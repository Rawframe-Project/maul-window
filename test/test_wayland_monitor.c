// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Monitor hotplug on the Wayland backend against the test compositor of
// wayland_server.h with an output of wayland_output_server.h: a
// wl_output plugged in while the program runs arrives as a monitor with
// its name, place, mode, physical size and scale; a new mode and scale
// change it; unplugged, it is removed and its id goes stale. Skipped
// (exit status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_output_server.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"

#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 32

typedef enum Phase
{
    phaseCreate,
    phaseAdded,
    phaseChanged,
    phaseRemoved,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    OutputServer output;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinMonitorId monitor;
    mwinEvent records[MAX_RECORDS];
    int count;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static const mwinEvent* Find(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

static size_t Monitors(const mwinContext* context)
{
    mwinMonitorId monitors[4];
    size_t count = 99;
    return mwinGetMonitors(context, monitors, 4, &count) == mwin_success ? count : 99;
}

static bool Same(mwinMonitorId a, mwinMonitorId b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventWindowCreated) != nullptr;
    case phaseAdded:
        return Find(program, mwin_eventMonitorAdded) != nullptr;
    case phaseChanged:
        return Find(program, mwin_eventMonitorChanged) != nullptr;
    case phaseRemoved:
        return Find(program, mwin_eventMonitorRemoved) != nullptr;
    default:
        return false;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    mwinMonitorInfo info;
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(Monitors(context) == 0, "no monitor before one is plugged in");
        OutputAdd(&program->output, program->server, "TEST-1", 1920, 0, 1280, 720, 2);
        break;
    case phaseAdded:
        program->monitor = Find(program, mwin_eventMonitorAdded)->data.monitor;
        CHECK(Monitors(context) == 1 &&
                  mwinGetMonitorInfo(context, program->monitor, &info) == mwin_success &&
                  info.nameLength == 6 && memcmp(info.name, "TEST-1", 6) == 0 &&
                  info.bounds.x == 1920 && info.bounds.y == 0 && info.bounds.width == 1280 &&
                  info.bounds.height == 720 && info.scale == 2.0f && info.widthMm == 300 &&
                  info.heightMm == 200 && info.refreshMilliHz == 60000,
              "a plugged in output arrives with its facts");
        OutputChange(&program->output, 1920, 1080, 1);
        break;
    case phaseChanged:
        CHECK(Same(Find(program, mwin_eventMonitorChanged)->data.monitor, program->monitor) &&
                  mwinGetMonitorInfo(context, program->monitor, &info) == mwin_success &&
                  info.bounds.width == 1920 && info.bounds.height == 1080 && info.scale == 1.0f,
              "a new mode and scale change the same monitor");
        OutputRemove(&program->output);
        break;
    default:
        CHECK(Same(Find(program, mwin_eventMonitorRemoved)->data.monitor, program->monitor) &&
                  Monitors(context) == 0 &&
                  mwinGetMonitorInfo(context, program->monitor, &info) == mwin_errorStale,
              "unplugged, it is removed and its id goes stale");
        break;
    }
    program->phase += 1;
    program->count = 0;
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
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
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
