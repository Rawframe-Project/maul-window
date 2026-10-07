// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend against a real X server (Xvfb in CI, with no window
// manager): the window a creation makes and its mapping, the monitors
// from RandR, the native handles, size and place as the X server
// reports them, hiding and showing, and the requests a server without a
// window manager cannot carry out; and a Wayland display that cannot
// be reached giving way to X11. Without DISPLAY the test is skipped
// (exit status 77).

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/native.h"

#include <stdlib.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull

typedef enum Phase
{
    phaseCreate,
    phaseResize,
    phaseMove,
    phaseHide,
    phaseShow,
    phaseMode,
    phaseDestroy,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    // What arrived since the phase began: a bit per event type, the last
    // size and place, and the last completion.
    uint64_t seen;
    mwinSize size;
    mwinPosition position;
    mwinCompletion completion;
    bool completed;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->seen |= 1ull << event.type;
        program->size = event.type == mwin_eventResized ? event.data.size : program->size;
        program->position = event.type == mwin_eventMoved ? event.data.position : program->position;
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
    case phaseHide:
        return Seen(program, mwin_eventHidden);
    case phaseShow:
        return Seen(program, mwin_eventShown);
    case phaseMode:
        return program->completed;
    default:
        return Seen(program, mwin_eventWindowDestroyed);
    }
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
    CHECK(program->completion.outcome == mwin_outcomeDone, "the window is made");
    mwinNativeHandles handles;
    CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success &&
              handles.platform == mwin_platformX11 && handles.handles.x11.connection != nullptr &&
              handles.handles.x11.window != 0,
          "the connection and the window");
    mwinMonitorId monitors[4];
    size_t count = 0;
    mwinMonitorInfo info;
    CHECK(mwinGetMonitors(context, monitors, 4, &count) == mwin_success && count >= 1 &&
              mwinGetMonitorInfo(context, monitors[0], &info) == mwin_success &&
              info.bounds.width > 0 && info.primary,
          "the monitors, the primary first");
}

// Checks what the phase brought, and starts the next.
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
        CHECK(mwinRequestVisible(context, window, false, nullptr) == mwin_success, "hide");
        break;
    case phaseHide:
        CHECK(mwinRequestVisible(context, window, true, nullptr) == mwin_success, "show");
        break;
    case phaseShow:
        CHECK(mwinRequestMode(context, window, mwin_modeMaximized, nullptr) == mwin_success,
              "maximize");
        break;
    case phaseMode:
        CHECK(program->completion.outcome == mwin_outcomeUnsupported,
              "no window manager, no maximizing");
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
    def.title = "Maul X11";
    def.titleLength = 8;
    def.size = (mwinSize){640.0f, 480.0f};
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
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 1000000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

static mwinResult QuickInit(mwinContext* context, void* user)
{
    (void)context;
    *(bool*)user = true;
    return mwin_success;
}

static mwinFrameResult QuickFrame(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_frameStop;
}

// A Wayland display that cannot be reached gives way to X11, and with
// neither reachable the run fails as the last one did.
static void TestFallback(void)
{
    // setenv may overwrite what getenv returned.
    char display[64];
    snprintf(display, sizeof(display), "%s", getenv("DISPLAY"));
    bool started = false;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = QuickInit;
    def.frame = QuickFrame;
    def.user = &started;
    // No gamepads: the quick runs leave the input devices to the gamepad
    // test running beside this one.
    def.context.limits.gamepads = 0;
    setenv("WAYLAND_DISPLAY", "mwin-no-such-display", 1);
    CHECK(mwinRun(&def) == mwin_success && started, "past an unreachable Wayland display to X11");
    started = false;
    setenv("DISPLAY", ":4242", 1);
    CHECK(mwinRun(&def) == mwin_errorPlatform && !started, "neither display reachable");
    setenv("DISPLAY", display, 1);
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        return 77;
    }
    TestFallback();
    // X11 even where a Wayland session runs.
    unsetenv("WAYLAND_DISPLAY");
    Program program = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the X server");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
