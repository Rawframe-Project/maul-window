// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend against a real compositor (a headless weston in
// CI): the window a first configure makes, the outputs as monitors, the
// native handles, the requests the protocol carries out or refuses, a
// mode the compositor grants, and destruction. Without WAYLAND_DISPLAY
// the test is skipped (exit status 77).

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/native.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

// Frames spin without waiting, so the compositor gets this long.
#define DEADLINE_NS 5000000000ull

typedef enum Phase
{
    phaseCreate,
    phaseRequests,
    phaseMode,
    phaseDestroy,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId waiting;
    // What arrived: a bit per event type, and the last completion.
    uint64_t seen;
    mwinCompletion completion;
    bool completed;
    mwinSize size;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Drain(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->seen |= 1ull << event.type;
        if (event.type == mwin_eventResized)
        {
            program->size = event.data.size;
        }
        if (event.type == mwin_eventRequestCompleted &&
            event.data.completion.request.index1 == program->waiting.index1 &&
            event.data.completion.request.generation == program->waiting.generation)
        {
            program->completion = event.data.completion;
            program->completed = true;
        }
    }
}

// Starts waiting for a request's completion.
static void Wait(Program* program, mwinRequestId request)
{
    program->waiting = request;
    program->completed = false;
    program->seen = 0;
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul Wayland";
    def.titleLength = 12;
    def.size = (mwinSize){640.0f, 480.0f};
    mwinRequestId request = {0};
    mwinResult status = mwinCreateWindow(context, &def, &program->window, &request);
    Wait(program, request);
    program->startNs = NowNs();
    return status;
}

static void CheckCreated(Program* program, mwinContext* context)
{
    CHECK(program->completion.outcome == mwin_outcomeDone, "the window is made");
    static const mwinEventType expected[] = {mwin_eventWindowCreated, mwin_eventScaleChanged,
                                             mwin_eventResized,       mwin_eventPixelSizeChanged,
                                             mwin_eventModeChanged,   mwin_eventShown};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        CHECK((program->seen & (1ull << expected[i])) != 0, "what the first configure tells");
    }
    mwinWindowState state;
    CHECK(mwinGetWindowState(context, program->window, &state) == mwin_success && state.created &&
              state.size.width > 0.0f && state.pixelSize.width > 0 && state.scale > 0.0f,
          "the state follows");
    mwinNativeHandles handles;
    CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success &&
              handles.platform == mwin_platformWayland &&
              handles.handles.wayland.display != nullptr &&
              handles.handles.wayland.surface != nullptr,
          "the display and the surface");
    mwinMonitorId monitors[4];
    size_t count = 0;
    mwinMonitorInfo info;
    CHECK(mwinGetMonitors(context, monitors, 4, &count) == mwin_success && count >= 1 &&
              mwinGetMonitorInfo(context, monitors[0], &info) == mwin_success &&
              info.bounds.width > 0 && info.scale >= 1.0f,
          "the compositor's outputs as monitors");
}

// Requests the protocol answers at once.
static void MakeRequests(Program* program, mwinContext* context)
{
    mwinRequestId request = {0};
    CHECK(mwinRequestTitle(context, program->window, "Retitled", 8, nullptr) == mwin_success &&
              mwinRequestSizeLimits(context, program->window, (mwinSize){200.0f, 150.0f},
                                    (mwinSize){0.0f, 0.0f}, nullptr) == mwin_success &&
              mwinRequestPosition(context, program->window, (mwinPosition){10.0f, 10.0f},
                                  nullptr) == mwin_success,
          "title, size limits, position");
    CHECK(mwinRequestSize(context, program->window, (mwinSize){800.0f, 600.0f}, &request) ==
              mwin_success,
          "a new size");
    Wait(program, request);
}

static void Step(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
        CheckCreated(program, context);
        MakeRequests(program, context);
        program->phase = phaseRequests;
        return;
    case phaseRequests:
    {
        CHECK(program->completion.outcome == mwin_outcomeDone && program->size.width == 800.0f &&
                  program->size.height == 600.0f,
              "a windowed toplevel sizes itself");
        mwinRequestId request = {0};
        CHECK(mwinRequestMode(context, program->window, mwin_modeMaximized, &request) ==
                  mwin_success,
              "maximize");
        Wait(program, request);
        program->phase = phaseMode;
        return;
    }
    case phaseMode:
    {
        mwinWindowState state;
        CHECK(mwinGetWindowState(context, program->window, &state) == mwin_success &&
                  (program->completion.outcome == mwin_outcomeDenied ||
                   (program->completion.outcome == mwin_outcomeDone &&
                    state.mode == mwin_modeMaximized)),
              "the next configure answers the mode");
        program->seen = 0;
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "destroy");
        program->phase = phaseDestroy;
        return;
    }
    default:
        CHECK((program->seen & (1ull << mwin_eventWindowDestroyed)) != 0, "destroyed");
        program->phase = phaseDone;
    }
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Drain(program, context);
    bool ready = program->phase == phaseDestroy ? program->seen != 0 : program->completed;
    if (ready)
    {
        Step(program, context);
        program->startNs = NowNs();
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
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

int main(void)
{
    const char* display = getenv("WAYLAND_DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        return 77;
    }
    Program program = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the compositor");
    CHECK(!program.timedOut, "the compositor answers in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
