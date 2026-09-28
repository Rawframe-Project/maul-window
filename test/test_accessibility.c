// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility hooks on the test backend:
// - a root the platform took is the window's, and none clears it;
// - a root the platform refused leaves the one before;
// - a client asking for the tree tells the program once a window;
// - a new window in the slot of one that had both starts with neither;
// - stale windows and a missing context are refused.

#include "core.h"
#include "test_program.h"

#include "maul-window/accessibility.h"
#include "maul-window/test.h"

static int s_first;
static int s_second;

static int Asked(const Program* program)
{
    int count = 0;
    for (int i = 0; i < program->eventCount; i++)
    {
        count += program->events[i].type == mwin_eventAccessibilityRequested;
    }
    return count;
}

static void* RootOf(const mwinContext* context, mwinWindowId window)
{
    return mwinFindWindow(context, window)->accessibilityRoot;
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinWindowDef def = mwinDefaultWindowDef();
    mwinWindowId window = program->windows[0];
    switch (step)
    {
    case 0:
        CHECK(mwinCreateWindow(context, &def, &program->windows[0], nullptr) == mwin_success,
              "a window");
        break;
    case 1:
        CHECK(RootOf(context, window) == nullptr, "a new window has no root");
        CHECK(mwinRequestAccessibilityRoot(context, window, &s_first, nullptr) == mwin_success,
              "a root");
        break;
    case 2:
        CHECK(RootOf(context, window) == &s_first, "a root the platform took is the window's");
        CHECK(mwinTestSetAnswer(context, mwin_requestAccessibilityRoot, mwin_outcomeUnsupported) ==
                      mwin_success &&
                  mwinRequestAccessibilityRoot(context, window, &s_second, nullptr) == mwin_success,
              "a root the platform refuses");
        break;
    case 3:
        CHECK(RootOf(context, window) == &s_first, "a refused root leaves the one before");
        CHECK(mwinTestSetAnswer(context, mwin_requestAccessibilityRoot, mwin_outcomeDone) ==
                      mwin_success &&
                  mwinRequestAccessibilityRoot(context, window, nullptr, nullptr) == mwin_success,
              "no root");
        break;
    case 4:
        CHECK(RootOf(context, window) == nullptr, "no root clears it");
        program->eventCount = 0;
        CHECK(mwinTestAskAccessibility(context, window) == mwin_success &&
                  mwinTestAskAccessibility(context, window) == mwin_success,
              "a client asks twice");
        break;
    case 5:
        CHECK(Asked(program) == 1, "the program told once a window");
        CHECK(mwinRequestAccessibilityRoot(context, window, &s_first, nullptr) == mwin_success,
              "a root again");
        break;
    case 6:
        CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroy");
        break;
    case 7:
        CHECK(mwinCreateWindow(context, &def, &program->windows[1], nullptr) == mwin_success &&
                  program->windows[1].index1 == window.index1,
              "a window in the same slot");
        break;
    case 8:
        program->eventCount = 0;
        CHECK(RootOf(context, program->windows[1]) == nullptr &&
                  mwinTestAskAccessibility(context, program->windows[1]) == mwin_success,
              "it starts with no root");
        break;
    default:
        CHECK(Asked(program) == 1, "and tells of its first client again");
        CHECK(mwinRequestAccessibilityRoot(context, window, &s_first, nullptr) == mwin_errorStale &&
                  mwinTestAskAccessibility(context, window) == mwin_errorStale &&
                  mwinRequestAccessibilityRoot(nullptr, window, nullptr, nullptr) ==
                      mwin_errorInvalid &&
                  mwinTestAskAccessibility(nullptr, window) == mwin_errorInvalid,
              "stale windows and a missing context refused");
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
