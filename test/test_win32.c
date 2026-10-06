// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend against Windows (a CI runner's desktop, or wine):
// the window a creation makes, the monitors, the native handles, size
// and place as Windows reports them, maximizing, borderless full screen
// and back, and the close button's message.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/native.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Generous: under wine on a loaded machine a phase has taken over 10 s.
#define DEADLINE_MS 30000u

typedef enum Phase
{
    phaseCreate,
    phaseResize,
    phaseMove,
    phaseMaximize,
    phaseFullscreen,
    phaseWindowed,
    phaseClose,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    ULONGLONG startMs;
    mwinWindowId window;
    uint64_t seen;
    mwinSize size;
    mwinPosition position;
    mwinWindowMode mode;
    mwinCompletion completion;
    bool completed;
    bool timedOut;
} Program;

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
        return program->size.width == 800.0f && program->size.height == 600.0f;
    case phaseMove:
        return program->position.x == 40.0f && program->position.y == 30.0f;
    case phaseMaximize:
        return program->completed && program->mode == mwin_modeMaximized;
    case phaseFullscreen:
        return program->completed && program->mode == mwin_modeBorderlessFullscreen;
    case phaseWindowed:
        return program->completed && program->mode == mwin_modeWindowed;
    default:
        return Seen(program, mwin_eventCloseRequested);
    }
}

static HWND WindowOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (HWND)handles.handles.win32.hwnd
               : nullptr;
}

static void CheckCreated(const Program* program, mwinContext* context)
{
    static const mwinEventType expected[] = {mwin_eventWindowCreated, mwin_eventScaleChanged,
                                             mwin_eventResized,       mwin_eventPixelSizeChanged,
                                             mwin_eventModeChanged,   mwin_eventShown};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        CHECK(Seen(program, expected[i]), "what a creation reports");
    }
    CHECK(program->completion.outcome == mwin_outcomeDone &&
              IsWindow(WindowOf(context, program->window)),
          "the window is made");
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
                  mwinRequestSize(context, window, (mwinSize){800.0f, 600.0f}, nullptr) ==
                      mwin_success,
              "a title and a size");
        break;
    case phaseResize:
        CHECK(mwinRequestPosition(context, window, (mwinPosition){40.0f, 30.0f}, nullptr) ==
                  mwin_success,
              "a place");
        break;
    case phaseMove:
        CHECK(mwinRequestMode(context, window, mwin_modeMaximized, nullptr) == mwin_success,
              "maximize");
        break;
    case phaseMaximize:
        CHECK(mwinRequestMode(context, window, mwin_modeBorderlessFullscreen, nullptr) ==
                  mwin_success,
              "full screen");
        break;
    case phaseFullscreen:
        CHECK(mwinRequestMode(context, window, mwin_modeWindowed, nullptr) == mwin_success,
              "windowed again");
        break;
    case phaseWindowed:
        CHECK(program->size.width == 800.0f || program->size.width == 0.0f, "the size from before");
        PostMessageW(WindowOf(context, window), WM_CLOSE, 0, 0);
        break;
    default:
        CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroy");
        break;
    }
    program->phase += 1;
    program->seen = 0;
    program->completed = false;
    program->startMs = GetTickCount64();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul Win32";
    def.titleLength = 10;
    def.size = (mwinSize){640.0f, 480.0f};
    program->startMs = GetTickCount64();
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
    else if (GetTickCount64() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d: mode %d, %s (outcome %d), %gx%g at %g,%g\n",
                     (int)program->phase, (int)program->mode,
                     program->completed ? "a request completed" : "no request completed",
                     (int)program->completion.outcome, (double)program->size.width,
                     (double)program->size.height, (double)program->position.x,
                     (double)program->position.y);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        Sleep(1);
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
    CHECK(mwinRun(&def) == mwin_success, "the program runs on Windows");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
