// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Window properties and native handles against the test backend: the
// style and opacity a creation leaves, requests for size limits, aspect
// ratio, style and opacity with their refusals, and the handle bundle of
// each surface generation.

#include "test_program.h"

#include "maul-window/native.h"

#include <math.h>

static void PropertyStep(Program* program, mwinContext* context, int step)
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
        CHECK(mwinGetWindowState(context, window, &state) == mwin_success &&
                  state.style == (mwin_styleResizable | mwin_styleDecorated) &&
                  state.opacity == 1.0f,
              "a new window's style and opacity");
        CHECK(mwinRequestSizeLimits(context, window, (mwinSize){320.0f, 200.0f},
                                    (mwinSize){0.0f, 0.0f}, nullptr) == mwin_success &&
                  mwinRequestAspectRatio(context, window, 16, 9, nullptr) == mwin_success &&
                  mwinRequestStyle(context, window, mwin_styleAlwaysOnTop, nullptr) ==
                      mwin_success &&
                  mwinRequestOpacity(context, window, 0.5f, nullptr) == mwin_success,
              "property requests");
        CHECK(mwinRequestSizeLimits(context, window, (mwinSize){320.0f, 200.0f},
                                    (mwinSize){100.0f, 0.0f}, nullptr) == mwin_errorInvalid &&
                  mwinRequestSizeLimits(context, window, (mwinSize){-1.0f, 0.0f},
                                        (mwinSize){0.0f, 0.0f}, nullptr) == mwin_errorInvalid &&
                  mwinRequestSizeLimits(context, window, (mwinSize){NAN, 0.0f},
                                        (mwinSize){0.0f, 0.0f}, nullptr) == mwin_errorInvalid &&
                  mwinRequestAspectRatio(context, window, 16, 0, nullptr) == mwin_errorInvalid &&
                  mwinRequestStyle(context, window, 8, nullptr) == mwin_errorInvalid &&
                  mwinRequestOpacity(context, window, 1.5f, nullptr) == mwin_errorInvalid &&
                  mwinRequestOpacity(context, window, NAN, nullptr) == mwin_errorInvalid,
              "crossed limits, a half ratio, unknown flags, an opacity out of range");
        return;
    }
    if (step == 2)
    {
        CHECK(program->eventCount == 4 &&
                  mwinGetWindowState(context, window, &state) == mwin_success &&
                  state.style == mwin_styleAlwaysOnTop && state.opacity == 0.5f,
              "carried out, the state follows");
        CHECK(mwinTestSetAnswer(context, mwin_requestOpacity, mwin_outcomeUnsupported) ==
                      mwin_success &&
                  mwinRequestOpacity(context, window, 0.25f, nullptr) == mwin_success,
              "a platform without blending");
        return;
    }
    CHECK(program->eventCount == 1 &&
              program->events[0].data.completion.outcome == mwin_outcomeUnsupported &&
              mwinGetWindowState(context, window, &state) == mwin_success && state.opacity == 0.5f,
          "a refused request leaves the state");
    program->done = true;
}

static void TestProperties(void)
{
    Program program = {.step = PropertyStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void HandleStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    mwinNativeHandles handles;
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        CHECK(mwinGetNativeHandles(context, program->windows[0], &handles) == mwin_errorState,
              "no surface before the platform makes the window");
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        CHECK(mwinGetNativeHandles(context, window, &handles) == mwin_success &&
                  handles.platform == mwin_platformTest && handles.surfaceGeneration == 1,
              "the first generation");
        mwinEvent lost = {.type = mwin_eventSurfaceLost, .window = window};
        CHECK(mwinTestPost(context, &lost) == mwin_success, "the surface goes");
        return;
    }
    if (step == 2)
    {
        CHECK(mwinGetNativeHandles(context, window, &handles) == mwin_errorState,
              "no handles without a surface");
        mwinEvent restored = {.type = mwin_eventSurfaceRestored, .window = window};
        CHECK(mwinTestPost(context, &restored) == mwin_success, "and comes back");
        return;
    }
    CHECK(mwinGetNativeHandles(context, window, &handles) == mwin_success &&
              handles.surfaceGeneration == 2,
          "the next generation, same window");
    CHECK(mwinDestroyWindow(context, window) == mwin_success &&
              mwinGetNativeHandles(context, window, &handles) == mwin_errorStale &&
              mwinGetNativeHandles(context, window, nullptr) == mwin_errorInvalid,
          "none for a destroyed window");
    program->done = true;
}

static void TestNativeHandles(void)
{
    Program program = {.step = HandleStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

int main(void)
{
    TestProperties();
    TestNativeHandles();
    return s_failures == 0 ? 0 : 1;
}
