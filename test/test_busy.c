// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Calls from inside the context's allocator on the test backend: a
// request, setters, a destroy and a drain, made while a clipboard write
// takes its copy and gives back the one before, each refused with
// mwin_errorBusy and counted as misuse; the outer write unchanged by them,
// and the same calls taken once the allocator returns.

#include "test_program.h"

#include "maul-window/clipboard.h"
#include "maul-window/gamepad.h"
#include "maul-window/test.h"

#include <stdlib.h>
#include <string.h>

#ifdef MWIN_TEST_GAMEPAD
enum
{
    INNER_CALLS = 5
};
#else
enum
{
    INNER_CALLS = 4
};
#endif

// What the allocator calls back into, and what it saw.
static mwinContext* s_context;
static mwinWindowId s_window;
#ifdef MWIN_TEST_GAMEPAD
static mwinGamepadId s_gamepad;
#endif
static bool s_armed;
static int s_allocations;
static int s_releases;
static int s_notBusy;

static void Expect(mwinResult status)
{
    s_notBusy += status != mwin_errorBusy;
}

// One call of each kind into the context, each of which changes it when
// it is taken.
static void Reenter(void)
{
    mwinRequestId request = {0};
    Expect(mwinWindow_RequestTitle(s_context, s_window, "Inner", 5, &request));
    Expect(mwinContext_SetTestTime(s_context, 1000000000));
    Expect(mwinDestroyWindow(s_context, s_window));
    mwinEvent event;
    Expect(mwinContext_NextEvent(s_context, &event));
#ifdef MWIN_TEST_GAMEPAD
    Expect(mwinGamepad_SetRumble(s_context, s_gamepad, 1.0f, 1.0f, 100));
#endif
}

static void* ReenterAllocate(size_t size, size_t alignment, void* user)
{
    (void)user;
    if (s_armed)
    {
        s_allocations += 1;
        Reenter();
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

static void ReenterFree(void* memory, size_t size, size_t alignment, void* user)
{
    (void)size;
    (void)alignment;
    (void)user;
    if (s_armed)
    {
        s_releases += 1;
        Reenter();
    }
    free(memory);
}

// The outcome of a request's completion among the drained records, or
// -1.
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

static bool Destroyed(const Program* program)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        if (program->events[i].type == mwin_eventWindowDestroyed)
        {
            return true;
        }
    }
    return false;
}

static bool OnClipboard(const mwinContext* context, const char* expected)
{
    char text[16];
    size_t length = 0;
    return mwinContext_GetTestClipboard(context, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

// A clipboard write with the allocator calling back in; the write must
// succeed as it would without.
static void ArmedWrite(Program* program, mwinContext* context, const char* text, int request)
{
    int calls = s_allocations + s_releases;
    int allocations = s_allocations;
    uint64_t misuse = mwinContext_GetMisuse(context);
    s_armed = true;
    mwinResult status = mwinWindow_RequestClipboardWrite(context, s_window, text, strlen(text),
                                                         &program->requests[request]);
    s_armed = false;
    calls = s_allocations + s_releases - calls;
    CHECK(status == mwin_success && program->requests[request].index1 != 0,
          "the outer write succeeds with its request");
    CHECK(s_allocations - allocations == 1, "the write takes its copy through the allocator");
    CHECK(s_notBusy == 0, "every call from inside the allocator refused with mwin_errorBusy");
    CHECK(mwinContext_GetMisuse(context) - misuse == (uint64_t)calls * INNER_CALLS,
          "each refused call counted once as misuse");
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    switch (step)
    {
    case 0:
        s_context = context;
        s_window = Create(context, &program->requests[0]);
#ifdef MWIN_TEST_GAMEPAD
        mwinGamepadInfo info = {.mapped = true, .capabilities = mwin_padRumble, .battery = -1};
        CHECK(mwinAddTestGamepad(context, &info, &s_gamepad) == mwin_success, "a gamepad");
#endif
        break;
    case 1:
        ArmedWrite(program, context, "Maul", 1);
        CHECK(mwinWindow_RequestTitle(context, s_window, "Outer", 5, &program->requests[2]) ==
                  mwin_success,
              "the same call taken once the allocator returns");
        break;
    case 2:
        CHECK(Outcome(program, 1) == mwin_outcomeDone && Outcome(program, 2) == mwin_outcomeDone &&
                  !Destroyed(program) && OnClipboard(context, "Maul"),
              "the write and the title done, the window kept");
        ArmedWrite(program, context, "Window", 3);
        CHECK(s_releases == 1, "the write gives back the copy before through the allocator");
        break;
    default:
        CHECK(Outcome(program, 3) == mwin_outcomeDone && !Destroyed(program) &&
                  OnClipboard(context, "Window"),
              "the second write done, the window kept");
        CHECK(mwinDestroyWindow(context, s_window) == mwin_success, "destroyed outside");
        program->done = true;
        break;
    }
}

int main(void)
{
    Program program = {.step = Step};
    mwinContextDef def = mwinDefaultContextDef();
    def.allocator = (mwinAllocator){ReenterAllocate, ReenterFree, nullptr};
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
