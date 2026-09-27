// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keeping the display awake over the session bus, on X11, against the
// stand-ins of linux_bus_fake.h: the screensaver inhibited with the
// program's name while the window asks and shows, let go while it is
// hidden and when it no longer asks; the portal asked with the idle
// flag when the screensaver refuses, and its handle closed; the
// screensaver let go at the program's end. Skipped (exit status 77)
// without an X server, dbus-daemon or libdbus-1.

#include "linux_bus_fake.h"
#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/services.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 10000000000ull

typedef enum Phase
{
    phaseCreate,
    phaseAwake,
    phaseHidden,
    phaseShown,
    phaseAsleep,
    phasePortal,
    phasePortalClosed,
    phaseEnding,
    phaseDone,
} Phase;

typedef struct Program
{
    FakeBus* fake;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
    int outcome;
    bool timedOut;
} Program;

static char s_directory[] = "/tmp/mwin-awake-XXXXXX";

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Ask(mwinContext* context, Program* program, bool awake)
{
    program->outcome = -1;
    CHECK(mwinRequestKeepAwake(context, program->window, awake, &program->request) == mwin_success,
          "ask to keep awake");
}

static void Show(mwinContext* context, Program* program, bool visible)
{
    CHECK(mwinRequestVisible(context, program->window, visible, nullptr) == mwin_success, "show");
}

static bool Ready(const Program* program)
{
    const FakeBus* fake = program->fake;
    bool done = program->outcome == mwin_outcomeDone;
    switch (program->phase)
    {
    case phaseCreate:
        return done;
    case phaseAwake:
    case phaseShown:
    case phaseEnding:
        return done && fake->inhibited == 1;
    case phaseHidden:
    case phaseAsleep:
        return fake->inhibited == 0;
    case phasePortal:
        return done && fake->portalHeld;
    case phasePortalClosed:
        return !fake->portalHeld;
    default:
        return false;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    FakeBus* fake = program->fake;
    switch (program->phase)
    {
    case phaseCreate:
        Ask(context, program, true);
        break;
    case phaseAwake:
        CHECK(strcmp(fake->application, "test_linux_awak") == 0 && fake->reason[0] != '\0',
              "the screensaver inhibited with the program's name and a reason");
        Show(context, program, false);
        break;
    case phaseHidden:
        Show(context, program, true);
        break;
    case phaseShown:
        Ask(context, program, false);
        break;
    case phaseAsleep:
        fake->refuseInhibit = true;
        Ask(context, program, true);
        break;
    case phasePortal:
        CHECK(fake->flags == 8u && fake->inhibited == 0,
              "the portal asked with the idle flag when the screensaver refuses");
        Ask(context, program, false);
        break;
    case phasePortalClosed:
        fake->refuseInhibit = false;
        Ask(context, program, true);
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
    def.size = (mwinSize){200.0f, 100.0f};
    program->startNs = NowNs();
    program->outcome = -1;
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    FakePump(program->fake);
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == program->request.index1 &&
            completion->request.generation == program->request.generation)
        {
            program->outcome = completion->outcome;
        }
    }
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 1000000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

// Waits for the stand-ins to hear what the program's end sent.
static bool Released(FakeBus* fake)
{
    for (int i = 0; i < 500 && fake->inhibited != 0; i++)
    {
        FakePump(fake);
        struct timespec pause = {0, 2000000};
        (void)nanosleep(&pause, nullptr);
    }
    return fake->inhibited == 0;
}

int main(void)
{
    static FakeBus fake;
    if (getenv("DISPLAY") == nullptr || mkdtemp(s_directory) == nullptr)
    {
        return 77;
    }
    (void)unsetenv("WAYLAND_DISPLAY");
    int status = 77;
    if (FakeStart(&fake, s_directory))
    {
        Program program = {.fake = &fake};
        mwinAppDef def = mwinDefaultAppDef();
        def.init = Init;
        def.frame = Frame;
        def.user = &program;
        CHECK(mwinRun(&def) == mwin_success, "the program runs");
        CHECK(!program.timedOut && program.phase == phaseDone, "every phase completes in time");
        CHECK(Released(&fake), "the screensaver let go at the program's end");
        status = s_failures == 0 ? 0 : 1;
    }
    FakeStop(&fake);
    FakeClean(s_directory);
    (void)rmdir(s_directory);
    return status;
}
