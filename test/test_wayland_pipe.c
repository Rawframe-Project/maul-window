// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's pipes (wayland_pipe.h), with no compositor: text
// written and the writer closed reads whole under a limit of its length
// or more, the largest limit included, whose byte past it does not fit
// 32 bits, and reads as too large under a smaller limit; a text that
// fills its buffer to exactly the limit reads whole; an allocator with
// no room fails the read, and so does a pipe that cannot be read.

#include "test_harness.h"
#include "wayland_pipe.h"

#include <string.h>
#include <unistd.h>

#define TEXT "a clipboard's text"

// Writes the text into a new pipe, closes the writer and reads under a
// limit until the reading ends: its outcome, the bytes in got.
static int ReadUnder(uint32_t limit, char* got, uint32_t* lengthOut)
{
    // Zero allocator: the C library's functions.
    static mwinContext context;
    mwinWaylandPipe pipe = {0};
    int writer = mwinWaylandOpenPipe(&pipe);
    if (writer < 0)
    {
        return -2;
    }
    ssize_t wrote = write(writer, TEXT, sizeof(TEXT) - 1);
    (void)close(writer);
    int outcome = wrote == (ssize_t)sizeof(TEXT) - 1 ? -1 : -2;
    for (int i = 0; i < 100 && outcome == -1; i++)
    {
        outcome = mwinWaylandReadPipe(&pipe, &context, limit);
    }
    *lengthOut = pipe.length;
    if (pipe.length <= sizeof(TEXT) && pipe.bytes != nullptr)
    {
        memcpy(got, pipe.bytes, pipe.length);
    }
    mwinWaylandClosePipe(&pipe, &context);
    return outcome;
}

// An allocator with no room.
static void* NoRoom(size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    return nullptr;
}

// A text of two first pieces, which the buffer holds at exactly a limit
// of its length, read under that limit.
static bool ReadsExactly(void)
{
    static mwinContext context;
    static char s_text[8192];
    memset(s_text, 'x', sizeof(s_text));
    mwinWaylandPipe pipe = {0};
    int writer = mwinWaylandOpenPipe(&pipe);
    if (writer < 0)
    {
        return false;
    }
    ssize_t wrote = write(writer, s_text, sizeof(s_text));
    (void)close(writer);
    int outcome = wrote == (ssize_t)sizeof(s_text) ? -1 : -2;
    for (int i = 0; i < 100 && outcome == -1; i++)
    {
        outcome = mwinWaylandReadPipe(&pipe, &context, sizeof(s_text));
    }
    bool whole = outcome == mwin_outcomeDone && pipe.length == sizeof(s_text);
    mwinWaylandClosePipe(&pipe, &context);
    return whole;
}

int main(void)
{
    char got[sizeof(TEXT)] = {0};
    uint32_t length = 0;
    CHECK(ReadUnder(UINT32_MAX, got, &length) == mwin_outcomeDone && length == sizeof(TEXT) - 1 &&
              memcmp(got, TEXT, length) == 0,
          "the whole text under the largest limit");
    CHECK(ReadUnder(sizeof(TEXT) - 1, got, &length) == mwin_outcomeDone &&
              length == sizeof(TEXT) - 1,
          "the whole text under a limit of its length");
    CHECK(ReadUnder(4, got, &length) == mwin_outcomeTooLarge, "too large under a smaller limit");
    CHECK(ReadsExactly(), "a text that fills its buffer to exactly the limit");
    static mwinContext s_full;
    s_full.allocator = (mwinAllocator){NoRoom, nullptr, nullptr};
    mwinWaylandPipe pipe = {0};
    int writer = mwinWaylandOpenPipe(&pipe);
    CHECK(writer >= 0 && mwinWaylandReadPipe(&pipe, &s_full, 64) == mwin_outcomeFailed,
          "an allocator with no room fails the read");
    if (writer >= 0)
    {
        (void)close(writer);
    }
    mwinWaylandClosePipe(&pipe, &s_full);
    // No descriptor: the read fails at once, not as one that waits.
    static mwinContext s_context;
    pipe = (mwinWaylandPipe){.fd = -1};
    CHECK(mwinWaylandReadPipe(&pipe, &s_context, 64) == mwin_outcomeFailed,
          "a pipe that cannot be read fails");
    mwinWaylandClosePipe(&pipe, &s_context);
    return s_failures == 0 ? 0 : 1;
}
