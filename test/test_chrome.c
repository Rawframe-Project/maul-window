// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Custom chrome on the test backend. The custom chrome style is a style
// like the others. Hit regions are refused at the call when there are
// too many, when they are not finite, when their size is negative or
// when their kind is unknown. The regions are copied at the call, done
// on the test backend, and replaced by the next call, and none clears
// them. A new window has none, also in the slot of one that had some.
// The one hit test every backend makes: the last region holding a point, with each rect's left and
// top edges in it and its right and bottom edges out, and the client everywhere else; and which
// kinds move or resize the window through the platform.

#include "chrome.h"
#include "test_program.h"

#include <math.h>
#include <string.h>

static bool Completed(const Program* program, mwinRequestId request, mwinOutcome outcome)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinCompletion* completion = &program->events[i].data.completion;
        if (program->events[i].type == mwin_eventRequestCompleted &&
            completion->request.index1 == request.index1 &&
            completion->request.generation == request.generation)
        {
            return completion->outcome == outcome;
        }
    }
    return false;
}

static bool Refused(mwinContext* context, mwinWindowId window, mwinHitRegion region)
{
    return mwinRequestHitRegions(context, window, &region, 1, nullptr) == mwin_errorInvalid;
}

static void CheckRefusals(mwinContext* context, mwinWindowId window)
{
    mwinHitRegion many[MWIN_HIT_REGIONS + 1] = {0};
    mwinHitRegion fine = {{0.0f, 0.0f, 10.0f, 10.0f}, mwin_hitCaption};
    mwinHitRegion notFinite = fine;
    notFinite.rect.width = INFINITY;
    mwinHitRegion nan = fine;
    nan.rect.x = NAN;
    mwinHitRegion negative = fine;
    negative.rect.height = -1.0f;
    mwinHitRegion unknown = fine;
    unknown.kind = mwin_hitClose + 1;
    CHECK(mwinRequestHitRegions(context, window, many, MWIN_HIT_REGIONS + 1, nullptr) ==
                  mwin_errorInvalid &&
              mwinRequestHitRegions(context, window, nullptr, 1, nullptr) == mwin_errorInvalid &&
              Refused(context, window, notFinite) && Refused(context, window, nan) &&
              Refused(context, window, negative) && Refused(context, window, unknown),
          "too many regions, none, not finite, of a negative size or an unknown kind refused");
    CHECK(mwinRequestHitRegions(nullptr, window, &fine, 1, nullptr) == mwin_errorInvalid &&
              mwinRequestHitRegions(context, (mwinWindowId){window.index1, window.generation + 1},
                                    &fine, 1, nullptr) == mwin_errorStale,
          "no context, a stale window");
}

// A caption across the top with a close button over its right end, and
// edges down the sides.
static const mwinHitRegion s_regions[4] = {
    {{0.0f, 0.0f, 200.0f, 30.0f}, mwin_hitCaption},
    {{170.0f, 0.0f, 30.0f, 30.0f}, mwin_hitClose},
    {{0.0f, 30.0f, 4.0f, 70.0f}, mwin_hitLeft},
    {{196.0f, 30.0f, 4.0f, 70.0f}, mwin_hitRight},
};

static void CheckHits(const mwinWindow* window)
{
    CHECK(mwinHitAt(window, 0.0f, 0.0f) == mwin_hitCaption &&
              mwinHitAt(window, 169.9f, 29.9f) == mwin_hitCaption &&
              mwinHitAt(window, 170.0f, 0.0f) == mwin_hitClose &&
              mwinHitAt(window, 199.9f, 10.0f) == mwin_hitClose,
          "the last region holding a point, its left and top edges in");
    CHECK(mwinHitAt(window, 200.0f, 10.0f) == mwin_hitClient &&
              mwinHitAt(window, 100.0f, 30.0f) == mwin_hitClient &&
              mwinHitAt(window, -0.1f, 10.0f) == mwin_hitClient &&
              mwinHitAt(window, 2.0f, 30.0f) == mwin_hitLeft &&
              mwinHitAt(window, 197.0f, 99.9f) == mwin_hitRight &&
              mwinHitAt(window, 197.0f, 100.0f) == mwin_hitClient,
          "right and bottom edges out, the client everywhere else");
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinWindowDef def = mwinDefaultWindowDef();
    def.style = mwin_styleResizable | mwin_styleCustomChrome;
    switch (step)
    {
    case 0:
        CHECK(mwinCreateWindow(context, &def, &program->windows[0], nullptr) == mwin_success,
              "a window of custom chrome");
        break;
    case 1:
    {
        const mwinWindow* window = mwinFindWindow(context, program->windows[0]);
        CheckRefusals(context, program->windows[0]);
        CHECK(window->regionCount == 0 && mwinHitAt(window, 1.0f, 1.0f) == mwin_hitClient,
              "a new window has no regions");
        mwinHitRegion regions[4];
        memcpy(regions, s_regions, sizeof(regions));
        CHECK(mwinRequestHitRegions(context, program->windows[0], regions, 4,
                                    &program->requests[0]) == mwin_success,
              "four regions");
        memset(regions, 0, sizeof(regions));
        CheckHits(window);
        break;
    }
    case 2:
    {
        const mwinWindow* window = mwinFindWindow(context, program->windows[0]);
        CHECK(Completed(program, program->requests[0], mwin_outcomeDone),
              "done on the test backend");
        mwinHitRegion top = {{0.0f, 0.0f, 200.0f, 4.0f}, mwin_hitTop};
        CHECK(mwinRequestHitRegions(context, program->windows[0], &top, 1, nullptr) ==
                      mwin_success &&
                  mwinHitAt(window, 10.0f, 2.0f) == mwin_hitTop &&
                  mwinHitAt(window, 10.0f, 10.0f) == mwin_hitClient,
              "replaced by the next call");
        CHECK(mwinRequestHitRegions(context, program->windows[0], nullptr, 0, nullptr) ==
                      mwin_success &&
                  mwinHitAt(window, 10.0f, 2.0f) == mwin_hitClient,
              "none clears them");
        mwinWindowState state;
        CHECK(mwinGetWindowState(context, program->windows[0], &state) == mwin_success &&
                  state.style == (mwin_styleResizable | mwin_styleCustomChrome) &&
                  mwinRequestStyle(context, program->windows[0], mwin_styleCustomChrome + 1,
                                   nullptr) == mwin_success &&
                  mwinRequestStyle(context, program->windows[0], 16, nullptr) == mwin_errorInvalid,
              "custom chrome a style like the others");
        CHECK(mwinRequestHitRegions(context, program->windows[0], s_regions, 4, nullptr) ==
                      mwin_success &&
                  mwinDestroyWindow(context, program->windows[0]) == mwin_success,
              "a window with regions destroyed");
        break;
    }
    case 3:
        // Its slot is free once its records are drained.
        CHECK(mwinCreateWindow(context, &def, &program->windows[1], nullptr) == mwin_success &&
                  program->windows[1].index1 == program->windows[0].index1,
              "its slot taken again");
        break;
    case 4:
        CHECK(mwinFindWindow(context, program->windows[1])->regionCount == 0,
              "a new window in the slot has none");
        break;
    default:
        CHECK(!mwinHitMoves(mwin_hitClient) && mwinHitMoves(mwin_hitCaption) &&
                  mwinHitMoves(mwin_hitTop) && mwinHitMoves(mwin_hitBottomRight) &&
                  !mwinHitMoves(mwin_hitMinimize) && !mwinHitMoves(mwin_hitMaximize) &&
                  !mwinHitMoves(mwin_hitClose),
              "captions and edges move or resize the window through the platform");
        program->done = true;
        break;
    }
}

int main(void)
{
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
