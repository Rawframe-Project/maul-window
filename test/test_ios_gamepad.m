// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend's gamepads against GameController, in the simulator
// (tools/run_ios_app.sh): the runtime watched from the start, its pads
// looked for and read at each pump for half a second; any pad found is
// mapped and named. No pad is attached to the simulator, so the tracking
// runs against a stand-in in pad_tracker, as on macOS, whose pad and
// motor code iOS shares.

#include "test_harness.h"

#include "maul-window/gamepad.h"
#include "maul-window/window.h"

#import <GameController/GameController.h>
#include <stdlib.h>
#include <time.h>

#define RUN_NS 500000000ull

typedef struct Program
{
    uint64_t startNs;
    int frames;
    bool listed;
    mwinWindowId window;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void CheckPads(Program* program, mwinContext* context)
{
    mwinGamepadId pads[8];
    size_t count = 0;
    program->listed = mwinGetGamepads(context, pads, 8, &count) == mwin_success;
    for (size_t i = 0; i < count && i < 8; i++)
    {
        mwinGamepadInfo info;
        CHECK(mwinGetGamepadInfo(context, pads[i], &info) == mwin_success && info.mapped &&
                  info.nameLength > 0,
              "a pad found is mapped and named");
    }
    CHECK(GCController.shouldMonitorBackgroundEvents, "GameController watched from the start");
    printf("%zu gamepads\n", count);
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    mwinWindowDef def = mwinDefaultWindowDef();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    program->frames += 1;
    if (NowNs() - program->startNs < RUN_NS)
    {
        return mwin_frameContinue;
    }
    CheckPads(program, context);
    return mwin_frameStop;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->frames > 1 && program->listed,
          "frames run with the pads watched, and list them");
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
