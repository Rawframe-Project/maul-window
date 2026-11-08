// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A touch through the system on iOS, in the simulator
// (tools/run_ios_app.sh): once the window shows, the test asks the
// runner to tap its middle through idb, which reaches the application
// through the simulator's own touch input and the system's hit test, as
// a finger does. The window never draws: before a first frame the
// system must still find it under the finger and give it the touch.
// The tap can take over ten seconds to come, most of it idb starting,
// so its phase waits longer than the others. The runner says when idb
// has done each tap (MWIN_TEST_RAN): a tap idb has done with no touch
// 20 seconds after it was lost on the way, and is asked for once more,
// three taps at most, each told with when idb did it, so that a run
// shows whether a touch came late or never.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#import <UIKit/UIKit.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEADLINE_NS 30000000000ull
#define TAPPED_NS   150000000000ull
// How long a touch may take after idb has done its tap, and the taps.
#define LOST_NS   20000000000ull
#define TAPS      3
#define SETTLE_NS 1000000000ull

typedef struct Program
{
    int phase;
    uint64_t startNs;
    uint64_t shownNs;
    mwinWindowId window;
    UIView* view;
    CGPoint tapped;
    // The taps asked for, and when the runner was first seen to have
    // done the last (0 before).
    int taps;
    uint64_t doneNs;
    int downs;
    int ups;
    uint64_t downId;
    uint64_t upId;
    mwinPosition down;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// When main began, which each phase's time counts from.
static uint64_t s_launchNs;

static double Since(void)
{
    return (double)(NowNs() - s_launchNs) / 1e9;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        if (event.kind == mwin_eventShown && program->shownNs == 0)
        {
            program->shownNs = NowNs();
        }
        if (event.kind == mwin_eventTouchDown)
        {
            program->downs += 1;
            program->downId = event.data.touch.id;
            program->down = event.data.touch.position;
        }
        if (event.kind == mwin_eventTouchUp)
        {
            program->ups += 1;
            program->upId = event.data.touch.id;
        }
    }
}

// Asks the runner for a tap on the window's middle.
static void Tap(Program* program)
{
    program->taps += 1;
    program->doneNs = 0;
    printf("idb: ui tap %d %d\n", (int)program->tapped.x, (int)program->tapped.y);
}

// How many of the test's lines the runner has run, 0 before any.
static int RunnerDone(void)
{
    const char* path = getenv("MWIN_TEST_RAN");
    FILE* file = path != nullptr ? fopen(path, "r") : nullptr;
    int done = 0;
    if (file != nullptr)
    {
        if (fscanf(file, "%d", &done) != 1)
        {
            done = 0;
        }
        (void)fclose(file);
    }
    return done;
}

// In the tap's phase, with no touch yet: notes when idb has done the
// last tap, and asks for another once it has been done for LOST_NS.
static void Watch(Program* program)
{
    if (program->phase != 1 || program->downs > 0 || program->ups > 0)
    {
        return;
    }
    if (program->doneNs == 0 && RunnerDone() >= program->taps)
    {
        program->doneNs = NowNs();
        printf("tap %d done by idb at %.2f s\n", program->taps, Since());
    }
    else if (program->doneNs != 0 && NowNs() - program->doneNs > LOST_NS && program->taps < TAPS)
    {
        printf("no touch %.0f s after tap %d: it was lost\n", (double)LOST_NS / 1e9, program->taps);
        Tap(program);
    }
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->view != nil && program->shownNs != 0 &&
               NowNs() - program->shownNs > SETTLE_NS;
    case 1:
        return program->ups >= 1;
    default:
        return true;
    }
}

static void Advance(Program* program)
{
    switch (program->phase)
    {
    case 0:
    {
        // The window fills the screen: its middle in screen points, as
        // idb takes them.
        CGRect bounds = program->view.window.bounds;
        program->tapped = CGPointMake(floor(CGRectGetMidX(bounds)), floor(CGRectGetMidY(bounds)));
        Tap(program);
        break;
    }
    case 1:
    {
        printf("touched after %d tap%s: %d down, %d up\n", program->taps,
               program->taps == 1 ? "" : "s", program->downs, program->ups);
        CHECK(program->downs >= 1 && program->ups >= 1 && program->downId == program->upId,
              "a finger down and up, through the system");
        CGPoint inView = [program->view convertPoint:program->tapped fromView:nil];
        CHECK(fabs((CGFloat)program->down.x - inView.x) < 2.0 &&
                  fabs((CGFloat)program->down.y - inView.y) < 2.0,
              "where it was tapped");
        break;
    }
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
    Collect(program, context);
    mwinNativeHandles handles;
    if (program->view == nil &&
        mwinWindow_GetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        program->view = (UIView*)handles.handles.apple.view;
    }
    if (program->phase == 2)
    {
        return mwin_frameStop;
    }
    Watch(program);
    if (Ready(program))
    {
        printf("phase %d at %.2f s\n", program->phase, Since());
        Advance(program);
    }
    else if (NowNs() - program->startNs > (program->phase == 1 ? TAPPED_NS : DEADLINE_NS))
    {
        printf("timed out in phase %d: %d down, %d up, %d tap%s\n", program->phase, program->downs,
               program->ups, program->taps, program->taps == 1 ? "" : "s");
        UIApplicationState state = UIApplication.sharedApplication.applicationState;
        printf("  UIKit: the application %s, the window %s\n",
               state == UIApplicationStateActive     ? "active"
               : state == UIApplicationStateInactive ? "inactive"
                                                     : "in the background",
               program->view.window.isKeyWindow ? "key" : "not key");
        s_failures += 1;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase == 2, "every phase ran");
    printf("result: %d failures\n", s_failures);
}

int main(void)
{
    static Program program;
    // The runner names the file to write to (tools/run_ios_app.sh).
    const char* out = getenv("MWIN_TEST_OUT");
    if (out != nullptr && freopen(out, "w", stdout) == nullptr)
    {
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    s_launchNs = NowNs();
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    // UIKit keeps the thread: this returns only if the program never ran.
    mwinResult result = mwinRun(&def);
    printf("result: mwinRun returned %d\n", (int)result);
    return 1;
}
