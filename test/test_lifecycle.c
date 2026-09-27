// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The lifecycle contract against the test backend: lifecycle and
// surface records come before all others, the platform's wait for the
// program runs a frame at once from inside the pump, suspending resets
// every window's input, a frame that stops from there ends the loop, and
// a lost surface shows in the window's state until it is restored.

#include "test_program.h"

static mwinEvent Global(mwinEventType type)
{
    mwinEvent event = {.type = type};
    return event;
}

static void SuspendStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        mwinEvent resize = {.type = mwin_eventResized, .window = window};
        resize.data.size = (mwinSize){300.0f, 200.0f};
        mwinEvent suspending = Global(mwin_eventSuspending);
        mwinEvent key = {.type = mwin_eventKeyDown, .window = window};
        CHECK(mwinTestPost(context, &resize) == mwin_success &&
                  mwinTestPost(context, &suspending) == mwin_success &&
                  mwinTestPost(context, &key) == mwin_success,
              "a resize, the suspend, then a key");
        return;
    }
    if (step == 2)
    {
        // The frame the pump ran for the suspend, before the key arrived.
        static const mwinEventType expected[] = {mwin_eventSuspending, mwin_eventResized,
                                                 mwin_eventInputStateReset};
        CHECK(Types(program, expected, 3), "the suspend first, at once, with a reset");
        CHECK(program->events[0].window.index1 == 0, "the context's records name no window");
        return;
    }
    CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventKeyDown,
          "what came after the suspend waits for the next frame");
    program->done = true;
}

static void TestSuspend(void)
{
    Program program = {.step = SuspendStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void CycleStep(Program* program, mwinContext* context, int step)
{
    if (step == 0)
    {
        static const mwinEventType cycle[] = {mwin_eventSuspending, mwin_eventSuspended,
                                              mwin_eventResuming, mwin_eventResumed};
        for (int i = 0; i < 4; i++)
        {
            mwinEvent event = Global(cycle[i]);
            CHECK(mwinTestPost(context, &event) == mwin_success, "a whole cycle");
        }
        return;
    }
    Drain(program, context);
    static const mwinEventType expected[] = {mwin_eventSuspending, mwin_eventSuspended,
                                             mwin_eventResuming, mwin_eventResumed};
    CHECK(program->eventCount == 1 && program->events[0].type == expected[step - 1],
          "each step of the cycle in a frame of its own");
    program->done = step == 4;
}

static void TestCycle(void)
{
    Program program = {.step = CycleStep};
    CHECK(Run(&program) == mwin_success && program.frame == 5, "four critical frames");
}

static void StopStep(Program* program, mwinContext* context, int step)
{
    if (step == 0)
    {
        mwinEvent suspending = Global(mwin_eventSuspending);
        CHECK(mwinTestPost(context, &suspending) == mwin_success, "the suspend");
        return;
    }
    CHECK(step == 1, "no frame after a critical frame stops");
    program->done = true;
}

static void TestStopFromCriticalFrame(void)
{
    Program program = {.step = StopStep};
    CHECK(Run(&program) == mwin_success && program.frame == 2 && program.quitCalls == 1,
          "a stop from the critical frame ends the loop and quits");
}

static void SurfaceStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    mwinWindowState state;
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        mwinEvent lost = {.type = mwin_eventSurfaceLost, .window = window};
        CHECK(mwinTestPost(context, &lost) == mwin_success, "the surface goes");
        return;
    }
    if (step == 2)
    {
        CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventSurfaceLost &&
                  mwinGetWindowState(context, window, &state) == mwin_success && state.surfaceLost,
              "the lost surface shows in the state");
        mwinEvent restored = {.type = mwin_eventSurfaceRestored, .window = window};
        CHECK(mwinTestPost(context, &restored) == mwin_success, "and comes back");
        return;
    }
    if (step == 3)
    {
        CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventSurfaceRestored &&
                  mwinGetWindowState(context, window, &state) == mwin_success && !state.surfaceLost,
              "restored");
        mwinEvent lost = {.type = mwin_eventSurfaceLost, .window = window};
        CHECK(mwinTestPost(context, &lost) == mwin_success, "lost again");
        return;
    }
    if (step == 4)
    {
        // Destroy it inside the frame that sees the loss.
        CHECK(program->eventCount == 1 && mwinDestroyWindow(context, window) == mwin_success,
              "destroyed");
        return;
    }
    CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventWindowDestroyed,
          "nothing of it but its end");
    program->done = true;
}

static void TestSurface(void)
{
    Program program = {.step = SurfaceStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void DestroyStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    if (step == 1)
    {
        Drain(program, context);
        mwinEvent lost = {.type = mwin_eventSurfaceLost, .window = window};
        CHECK(mwinTestPost(context, &lost) == mwin_success, "the surface goes");
        return;
    }
    if (step == 2)
    {
        // The critical frame destroys the window without draining.
        CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroyed");
        return;
    }
    Drain(program, context);
    CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventWindowDestroyed,
          "the loss of a destroyed window's surface is dropped");
    program->done = true;
}

static void TestDestroyDropsCritical(void)
{
    Program program = {.step = DestroyStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

int main(void)
{
    TestSuspend();
    TestCycle();
    TestStopFromCriticalFrame();
    TestSurface();
    TestDestroyDropsCritical();
    return s_failures == 0 ? 0 : 1;
}
