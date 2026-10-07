// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The platform services' contract on the test backend: addresses other
// than http, https and mailto, or with spaces or control characters,
// refused at the call, as are paths that are not absolute or hold a
// NUL; an address opened, a later one superseding a waiting one, a path
// revealed, and the window kept awake, its state saying so while a
// refusal leaves it; and the text of a request cancelled with its
// window, or never answered at the context's end, given back (the
// sanitizer builds would report a leak), and for a stale window given
// back with the size it was lent.

#include "test_program.h"

#include "maul-window/services.h"
#include "maul-window/test.h"

#include <stdlib.h>
#include <string.h>

// An absolute path on the platform built for, and its bytes.
#ifdef _WIN32
#define ROOT "C:"
#else
#define ROOT ""
#endif
#define PATH(text) ROOT text, sizeof(ROOT text) - 1

static int Outcome(const Program* program, int request)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        if (event->type == mwin_eventRequestCompleted &&
            SameId(event->data.completion.request, program->requests[request]))
        {
            return event->data.completion.outcome;
        }
    }
    return -1;
}

static bool Opened(mwinContext* context, mwinRequestKind kind, const char* expected)
{
    char text[64];
    size_t length = 0;
    return mwinTestGetOpened(context, kind, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static bool Refused(mwinContext* context, mwinWindowId window, const char* url, size_t length)
{
    return mwinRequestOpenUrl(context, window, url, length, nullptr) == mwin_errorInvalid;
}

static void CheckRefusals(mwinContext* context, mwinWindowId window)
{
    static char s_long[MWIN_ADDRESS_BYTES + 1];
    memcpy(s_long, "https://", 8);
    memset(s_long + 8, 'a', sizeof(s_long) - 8);
    CHECK(Refused(context, window, "ftp://x", 7) && Refused(context, window, "http://", 7) &&
              Refused(context, window, "http://a b", 10) &&
              Refused(context, window, "http://a\x01", 9) &&
              Refused(context, window, "javascript:alert(1)", 19) &&
              Refused(context, window, "http://\xC3", 8) &&
              Refused(context, window, s_long, sizeof(s_long)) &&
              Refused(context, window, nullptr, 8) &&
              mwinRequestOpenUrl(nullptr, window, "http://a", 8, nullptr) == mwin_errorInvalid,
          "addresses this does not open refused at the call");
    CHECK(mwinRequestRevealFile(context, window, "a/b", 3, nullptr) == mwin_errorInvalid &&
              mwinRequestRevealFile(context, window, PATH("/tmp/a\0b"), nullptr) ==
                  mwin_errorInvalid &&
              mwinRequestRevealFile(context, window, nullptr, 0, nullptr) == mwin_errorInvalid &&
              mwinRequestKeepAwake(context, (mwinWindowId){0}, true, nullptr) == mwin_errorStale,
          "paths that are not absolute or hold a NUL refused, and a stale window");
#ifdef _WIN32
    CHECK(mwinRequestRevealFile(context, window, "/tmp/x", 6, nullptr) == mwin_errorInvalid &&
              mwinRequestRevealFile(context, window, "\\\\host\\x", 8, nullptr) == mwin_success,
          "a path without its drive refused, and a share taken");
#endif
}

static void Ask(Program* program, mwinContext* context)
{
    mwinWindowId window = program->windows[0];
    CheckRefusals(context, window);
    CHECK(mwinRequestOpenUrl(context, window, "HTTPS://first", 13, &program->requests[1]) ==
                  mwin_success &&
              mwinRequestOpenUrl(context, window, "mailto:a@b.c", 12, &program->requests[2]) ==
                  mwin_success &&
              mwinRequestRevealFile(context, window, PATH("/tmp/\xC3\xA9.txt"),
                                    &program->requests[3]) == mwin_success &&
              mwinRequestKeepAwake(context, window, true, &program->requests[4]) == mwin_success,
          "two addresses, a path, and awake");
}

static void CheckDone(Program* program, mwinContext* context)
{
    mwinWindowState state;
    CHECK(Outcome(program, 1) == mwin_outcomeSuperseded &&
              Outcome(program, 2) == mwin_outcomeDone &&
              Opened(context, mwin_requestOpenUrl, "mailto:a@b.c"),
          "the later address opened, the first superseded");
    CHECK(Outcome(program, 3) == mwin_outcomeDone &&
              Opened(context, mwin_requestRevealFile, ROOT "/tmp/\xC3\xA9.txt"),
          "the path revealed");
    CHECK(Outcome(program, 4) == mwin_outcomeDone &&
              mwinGetWindowState(context, program->windows[0], &state) == mwin_success &&
              state.awake,
          "the window keeps the display awake");
    CHECK(mwinTestSetAnswer(context, mwin_requestKeepAwake, mwin_outcomeDenied) == mwin_success &&
              mwinRequestKeepAwake(context, program->windows[0], false, &program->requests[5]) ==
                  mwin_success,
          "a refused change");
}

static void CheckDenied(Program* program, mwinContext* context)
{
    mwinWindowState state;
    CHECK(Outcome(program, 5) == mwin_outcomeDenied &&
              mwinGetWindowState(context, program->windows[0], &state) == mwin_success &&
              state.awake,
          "a refusal leaves the window awake");
    // Held: one request is cancelled with its window, one never answered.
    program->windows[1] = Create(context, nullptr);
    CHECK(mwinTestHold(context, true) == mwin_success &&
              mwinRequestOpenUrl(context, program->windows[0], "http://gone", 11,
                                 &program->requests[6]) == mwin_success &&
              mwinRequestRevealFile(context, program->windows[1], PATH("/tmp/x"), nullptr) ==
                  mwin_success &&
              mwinDestroyWindow(context, program->windows[0]) == mwin_success,
          "requests held, one window destroyed");
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        break;
    case 1:
        Ask(program, context);
        break;
    case 2:
        CheckDone(program, context);
        break;
    case 3:
        CheckDenied(program, context);
        break;
    default:
        CHECK(Outcome(program, 6) == mwin_outcomeCancelled, "cancelled with its window");
        program->done = true;
        break;
    }
}

// An allocator that keeps the bytes it lends, so that a block given back
// with another size shows.
static size_t s_live = 0;

static void* CountAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_live += size;
    return malloc(size);
}

static void CountFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_live -= size;
    free(memory);
}

// A request for a stale window gives its copy of the address back whole.
static void StaleStep(Program* program, mwinContext* context, int step)
{
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        CHECK(mwinDestroyWindow(context, program->windows[0]) == mwin_success, "destroyed");
        return;
    }
    CHECK(mwinRequestOpenUrl(context, program->windows[0], "https://a", 9, nullptr) ==
                  mwin_errorStale &&
              mwinRequestRevealFile(context, program->windows[0], PATH("/tmp"), nullptr) ==
                  mwin_errorStale,
          "an address and a path for a window gone");
    program->done = true;
}

int main(void)
{
    Program stale = {.step = StaleStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.allocator = (mwinAllocator){CountAlloc, CountFree, nullptr};
    CHECK(RunWith(&stale, def) == mwin_success && stale.done && s_live == 0,
          "every byte given back as lent");
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
