// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's reads of another client's text at the edges of a
// clipboardBytes limit that is no whole number of 32-bit words: an
// empty text read as empty; a text of exactly the limit read whole; one
// byte more too large; and a text the allocator has no room for failed.
// The clipboard's hidden window made, the window's own events still
// arrive. Skipped (exit status 77) without DISPLAY.

#include "test_harness.h"
#include "x11_peer.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   50000000ull
#define LIMIT       10u
// The length of the text the allocator refuses, which nothing else
// asks for.
#define REFUSED 7u

typedef enum Phase
{
    phaseCreate,
    phaseEmpty,
    phaseExact,
    phaseOver,
    phaseRefused,
    phaseResized,
    phaseDone,
} Phase;

typedef struct Program
{
    Peer* peer;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
    int outcome;
    bool created;
    bool resized;
    bool reading;
    bool timedOut;
} Program;

// Refuses the bytes of a read REFUSED long while set.
static bool s_refuse = false;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)context;
    if (s_refuse && size == REFUSED && alignment == 1)
    {
        return nullptr;
    }
    return aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
}

static void Release(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        program->created = program->created || event.type == mwin_eventWindowCreated;
        program->resized = program->resized || event.type == mwin_eventPixelSizeChanged;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == program->request.index1 &&
            completion->request.generation == program->request.generation)
        {
            program->outcome = completion->outcome;
        }
    }
}

static bool Found(mwinContext* context, const char* expected, size_t length)
{
    char text[LIMIT];
    size_t found = 99;
    return mwinGetClipboardText(context, text, sizeof(text), &found) == mwin_success &&
           found == length && memcmp(text, expected, length) == 0;
}

// Whether the phase is done: once the program has heard the other
// client took CLIPBOARD, its read answered.
static bool Ready(Program* program, mwinContext* context)
{
    if (program->phase == phaseCreate)
    {
        return program->created;
    }
    if (program->phase == phaseResized)
    {
        return program->resized;
    }
    if (!program->reading && NowNs() - program->startNs >= SETTLE_NS)
    {
        CHECK(mwinRequestClipboardRead(context, program->window, &program->request) == mwin_success,
              "a read");
        program->reading = true;
    }
    return program->reading && program->outcome >= 0;
}

static void Advance(Program* program, mwinContext* context)
{
    static const char s_text[] = "0123456789!";
    Peer* peer = program->peer;
    int outcome = program->outcome;
    program->outcome = -1;
    program->reading = false;
    switch (program->phase)
    {
    case phaseCreate:
        PeerOwn(peer, peer->clipboard, peer->utf8, s_text, 0);
        break;
    case phaseEmpty:
        CHECK(outcome == mwin_outcomeDone && Found(context, "", 0), "an empty text read as empty");
        PeerOwn(peer, peer->clipboard, peer->utf8, s_text, LIMIT);
        break;
    case phaseExact:
        CHECK(outcome == mwin_outcomeDone && Found(context, s_text, LIMIT),
              "a text of exactly the limit read whole");
        PeerOwn(peer, peer->clipboard, peer->utf8, s_text, LIMIT + 1);
        break;
    case phaseOver:
        CHECK(outcome == mwin_outcomeTooLarge, "one byte more too large");
        PeerOwn(peer, peer->clipboard, peer->utf8, s_text, REFUSED);
        s_refuse = true;
        break;
    case phaseRefused:
        CHECK(outcome == mwin_outcomeFailed, "a text the allocator has no room for failed");
        s_refuse = false;
        program->resized = false;
        CHECK(mwinRequestSize(context, program->window, (mwinSize){300.0f, 200.0f}, nullptr) ==
                  mwin_success,
              "a new size");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    PeerPump(program->peer);
    Collect(program, context);
    if (Ready(program, context))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 500000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    static Peer peer;
    if (display == nullptr || display[0] == '\0' || !PeerStart(&peer))
    {
        return 77;
    }
    static char s_got[PEER_PIECE];
    peer.got = s_got;
    unsetenv("WAYLAND_DISPLAY");
    Program program = {.peer = &peer, .outcome = -1};
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.clipboardBytes = LIMIT;
    def.context.allocator = (mwinAllocator){Allocate, Release, nullptr};
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    xcb_disconnect(peer.connection);
    return s_failures == 0 ? 0 : 1;
}
