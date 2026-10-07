// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland seat's devices as the compositor changes them, against the
// test compositor of wayland_server.h: the keyboard, pointer and touch
// screen are made at the start, released when the seat loses them, made
// again when it has them back, and released with the seat when its
// global goes. Skipped (exit status 77) without XDG_RUNTIME_DIR or xkb
// data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"

#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define ALL (WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH)

typedef enum Phase
{
    phaseCreate,
    phaseLost,
    phaseBack,
    phaseGone,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    bool created;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static bool Ready(const Program* program)
{
    int made = 0;
    int released = 0;
    int seats = 0;
    ServerDevices(program->server, &made, &released, &seats);
    switch (program->phase)
    {
    case phaseCreate:
        return program->created && made == 3;
    case phaseLost:
        return released == 3;
    case phaseBack:
        return made == 6;
    case phaseGone:
        return released == 6 && seats == 1;
    default:
        return false;
    }
}

static void Advance(Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        ServerCapabilities(program->server, 0);
        break;
    case phaseLost:
        ServerCapabilities(program->server, ALL);
        break;
    case phaseBack:
        ServerRemoveSeat(program->server);
        break;
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
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->created = program->created || event.type == mwin_eventWindowCreated;
    }
    if (Ready(program))
    {
        Advance(program);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 500000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    static Server server;
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&server, "us", ""))
    {
        return 77;
    }
    static Program program;
    program = (Program){.server = &server};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    return s_failures == 0 ? 0 : 1;
}
