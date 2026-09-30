// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The macOS backend against AppKit (a CI runner's session): what a
// creation reports, the monitors, the native handles with the view's
// CAMetalLayer, a title, a size and a place as AppKit reports them,
// maximizing and back, hiding and showing, and destroying.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/native.h"

#include <time.h>

#define DEADLINE_NS 10000000000ull

typedef enum Phase
{
    phaseCreate,
    phaseResize,
    phaseMove,
    phaseMaximize,
    phaseWindowed,
    phaseHide,
    phaseShow,
    phaseDestroy,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    uint64_t seen;
    mwinSize size;
    mwinPosition position;
    mwinWindowMode mode;
    mwinCompletion completion;
    bool completed;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->seen |= 1ull << event.type;
        program->size = event.type == mwin_eventResized ? event.data.size : program->size;
        program->position = event.type == mwin_eventMoved ? event.data.position : program->position;
        program->mode = event.type == mwin_eventModeChanged ? event.data.mode : program->mode;
        if (event.type == mwin_eventRequestCompleted)
        {
            program->completion = event.data.completion;
            program->completed = true;
        }
    }
}

static bool Seen(const Program* program, mwinEventType type)
{
    return (program->seen & (1ull << type)) != 0;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return program->completed && Seen(program, mwin_eventShown);
    case phaseResize:
        return program->size.width == 400.0f && program->size.height == 300.0f;
    case phaseMove:
        return program->position.x == 120.0f && program->position.y == 200.0f;
    case phaseMaximize:
        return program->completed && program->mode == mwin_modeMaximized;
    case phaseWindowed:
        return program->completed && program->mode == mwin_modeWindowed;
    case phaseHide:
        return program->completed && Seen(program, mwin_eventHidden);
    case phaseShow:
        return program->completed && Seen(program, mwin_eventShown);
    default:
        return Seen(program, mwin_eventWindowDestroyed);
    }
}

static void CheckCreated(const Program* program, mwinContext* context)
{
    static const mwinEventType expected[] = {mwin_eventWindowCreated, mwin_eventScaleChanged,
                                             mwin_eventResized,       mwin_eventPixelSizeChanged,
                                             mwin_eventModeChanged,   mwin_eventMoved,
                                             mwin_eventShown};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        CHECK(Seen(program, expected[i]), "what a creation reports");
    }
    mwinNativeHandles handles;
    CHECK(program->completion.outcome == mwin_outcomeDone &&
              mwinGetNativeHandles(context, program->window, &handles) == mwin_success &&
              handles.platform == mwin_platformMacOS && handles.handles.apple.view != nullptr &&
              handles.handles.apple.layer != nullptr,
          "the window is made, with its view and layer");
    CHECK(program->size.width == 320.0f && program->size.height == 240.0f, "the size asked for");
    mwinMonitorId monitors[4];
    size_t count = 0;
    mwinMonitorInfo info;
    CHECK(mwinGetMonitors(context, monitors, 4, &count) == mwin_success && count >= 1 &&
              mwinGetMonitorInfo(context, monitors[0], &info) == mwin_success &&
              info.bounds.width > 0 && info.primary && info.scale >= 1.0f,
          "the monitors, the primary first");
}

static void Advance(Program* program, mwinContext* context)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case phaseCreate:
        CheckCreated(program, context);
        CHECK(mwinRequestTitle(context, window, "Retitled", 8, nullptr) == mwin_success &&
                  mwinRequestSize(context, window, (mwinSize){400.0f, 300.0f}, nullptr) ==
                      mwin_success,
              "a title and a size");
        break;
    case phaseResize:
        CHECK(mwinRequestPosition(context, window, (mwinPosition){120.0f, 200.0f}, nullptr) ==
                  mwin_success,
              "a place");
        break;
    case phaseMove:
        CHECK(mwinRequestMode(context, window, mwin_modeMaximized, nullptr) == mwin_success,
              "maximize");
        break;
    case phaseMaximize:
        CHECK(mwinRequestMode(context, window, mwin_modeWindowed, nullptr) == mwin_success,
              "windowed again");
        break;
    case phaseWindowed:
        CHECK(mwinRequestVisible(context, window, false, nullptr) == mwin_success, "hide");
        break;
    case phaseHide:
        CHECK(mwinRequestVisible(context, window, true, nullptr) == mwin_success, "show");
        break;
    case phaseShow:
        CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroy");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->seen = 0;
    program->completed = false;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS";
    def.titleLength = 10;
    def.size = (mwinSize){320.0f, 240.0f};
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        printf("timed out in phase %d\n", (int)program->phase);
        return mwin_frameStop;
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on macOS");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
