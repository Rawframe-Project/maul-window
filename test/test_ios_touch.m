// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A touch through the system on iOS, in the simulator
// (tools/run_ios_app.sh): once the window shows, the test asks the
// runner to tap its middle through idb, which reaches the application
// through the simulator's own touch input and the system's hit test, as
// a finger does. The window never draws: before a first frame the
// system must still find it under the finger and give it the touch.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#import <UIKit/UIKit.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define DEADLINE_NS 30000000000ull
#define SETTLE_NS   1000000000ull

typedef struct Program
{
    int phase;
    uint64_t startNs;
    uint64_t shownNs;
    mwinWindowId window;
    UIView* view;
    CGPoint tapped;
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
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventShown && program->shownNs == 0)
        {
            program->shownNs = NowNs();
        }
        if (event.type == mwin_eventTouchDown)
        {
            program->downs += 1;
            program->downId = event.data.touch.id;
            program->down = event.data.touch.position;
        }
        if (event.type == mwin_eventTouchUp)
        {
            program->ups += 1;
            program->upId = event.data.touch.id;
        }
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
        printf("idb: ui tap %d %d\n", (int)program->tapped.x, (int)program->tapped.y);
        break;
    }
    case 1:
    {
        CHECK(program->downs == 1 && program->ups == 1 && program->downId == program->upId,
              "one finger down and up, through the system");
        CGPoint inView = [program->view convertPoint:program->tapped fromView:nil];
        CHECK(fabs(program->down.x - inView.x) < 2.0 && fabs(program->down.y - inView.y) < 2.0,
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
        mwinGetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        program->view = (UIView*)handles.handles.apple.view;
    }
    if (program->phase == 2)
    {
        return mwin_frameStop;
    }
    if (Ready(program))
    {
        printf("phase %d at %.2f s\n", program->phase, Since());
        Advance(program);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d: %d down, %d up\n", program->phase, program->downs,
               program->ups);
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
