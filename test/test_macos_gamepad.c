// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The macOS backend's gamepads against GameController (a CI runner's
// session): the runtime watched from the start, its pads looked for and
// read at each pump for half a second, and let go at the end; any pad
// found is mapped and named. No pad is attached to the runner, so its
// tracking runs against a stand-in in pad_tracker.

#include "test_harness.h"

#include "maul-window/gamepad.h"

#include <time.h>

#define RUN_NS 500000000ull

typedef struct Program
{
    uint64_t startNs;
    int frames;
    bool listed;
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
    printf("%zu gamepads\n", count);
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    Program* program = user;
    program->startNs = NowNs();
    return mwin_success;
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

int main(void)
{
    Program program = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on macOS");
    CHECK(program.frames > 1 && program.listed, "frames run with the pads watched, and list them");
    return s_failures == 0 ? 0 : 1;
}
