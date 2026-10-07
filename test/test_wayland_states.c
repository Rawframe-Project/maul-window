// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Wayland window's states as the compositor configures them, against
// the test compositor of wayland_server.h:
// - a window made maximized asks the compositor for it, and has focus
//   once configured activated;
// - a largest size with a free height leaves the height free;
// - a configure that changes the height alone resizes the window;
// - suspended, the window is occluded; neither activated nor suspended,
//   it loses the focus and is revealed;
// - minimized, it stays so through a configure that does not activate
//   it, and is restored by one that does;
// - a maximized mode asked for and configured is done;
// - fullscreen and maximized states together are fullscreen.
// Skipped (exit status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"

#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   100000000ull
#define MAX_RECORDS 32
// The caption of the frame the backend draws, which the compositor's
// sizes hold above the content.
#define CAPTION 28

typedef enum Phase
{
    phaseCreate,
    phaseLimits,
    phaseResized,
    phaseOccluded,
    phaseUnfocused,
    phaseMinimized,
    phaseStillMinimized,
    phaseRestored,
    phaseAskMaximized,
    phaseMaximized,
    phaseFullscreen,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
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

static bool ModeIs(const Program* program, mwinWindowMode mode)
{
    const mwinEvent* changed = Find(program, mwin_eventModeChanged);
    return changed != nullptr && changed->data.mode == mode;
}

static void Configure(const Program* program, int32_t width, int32_t height, const uint32_t* states,
                      int count)
{
    ServerConfigure(program->server, width, height, states, count);
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventWindowCreated) != nullptr;
    case phaseLimits:
        return ServerShell(program->server).maxSize[0] == 300;
    case phaseResized:
        return Find(program, mwin_eventResized) != nullptr;
    case phaseOccluded:
        return Find(program, mwin_eventOccluded) != nullptr;
    case phaseUnfocused:
        return Find(program, mwin_eventFocusLost) != nullptr &&
               Find(program, mwin_eventRevealed) != nullptr;
    case phaseMinimized:
    case phaseRestored:
    case phaseFullscreen:
        return Find(program, mwin_eventModeChanged) != nullptr;
    case phaseStillMinimized:
        return NowNs() - program->startNs >= SETTLE_NS;
    case phaseAskMaximized:
        return ServerShell(program->server).maximizes >= 2;
    case phaseMaximized:
        return Find(program, mwin_eventRequestCompleted) != nullptr;
    default:
        return false;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    static const uint32_t s_activated[] = {XDG_TOPLEVEL_STATE_ACTIVATED};
    static const uint32_t s_suspended[] = {XDG_TOPLEVEL_STATE_ACTIVATED,
                                           XDG_TOPLEVEL_STATE_SUSPENDED};
    static const uint32_t s_maximized[] = {XDG_TOPLEVEL_STATE_MAXIMIZED,
                                           XDG_TOPLEVEL_STATE_ACTIVATED};
    static const uint32_t s_both[] = {XDG_TOPLEVEL_STATE_FULLSCREEN, XDG_TOPLEVEL_STATE_MAXIMIZED,
                                      XDG_TOPLEVEL_STATE_ACTIVATED};
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(ServerShell(program->server).maximizes == 1,
              "a window made maximized asks the compositor for it");
        CHECK(Find(program, mwin_eventFocusGained) != nullptr,
              "configured activated, it has focus");
        CHECK(mwinRequestSizeLimits(context, program->window, (mwinSize){0.0f, 0.0f},
                                    (mwinSize){300.0f, 0.0f}, nullptr) == mwin_success,
              "a largest width");
        break;
    case phaseLimits:
        CHECK(ServerShell(program->server).maxSize[1] == 0, "a free height left free");
        Configure(program, 640, 400 + CAPTION, s_activated, 1);
        break;
    case phaseResized:
    {
        const mwinEvent* resized = Find(program, mwin_eventResized);
        CHECK(resized->data.size.width == 640.0f && resized->data.size.height == 400.0f,
              "a configure that changes the height alone resizes the window");
        Configure(program, 0, 0, s_suspended, 2);
        break;
    }
    case phaseOccluded:
        Configure(program, 0, 0, nullptr, 0);
        break;
    case phaseUnfocused:
        CHECK(mwinRequestMode(context, program->window, mwin_modeMinimized, nullptr) ==
                  mwin_success,
              "minimize");
        break;
    case phaseMinimized:
        CHECK(ModeIs(program, mwin_modeMinimized), "minimized");
        Configure(program, 0, 0, nullptr, 0);
        break;
    case phaseStillMinimized:
        CHECK(Find(program, mwin_eventModeChanged) == nullptr,
              "a configure that does not activate it leaves it minimized");
        Configure(program, 0, 0, s_activated, 1);
        break;
    case phaseRestored:
        CHECK(ModeIs(program, mwin_modeWindowed), "one that activates it restores it");
        CHECK(mwinRequestMode(context, program->window, mwin_modeMaximized, &program->request) ==
                  mwin_success,
              "maximize");
        break;
    case phaseAskMaximized:
        Configure(program, 0, 0, s_maximized, 2);
        break;
    case phaseMaximized:
        CHECK(Find(program, mwin_eventRequestCompleted)->data.completion.outcome ==
                      mwin_outcomeDone &&
                  ModeIs(program, mwin_modeMaximized),
              "a maximized mode asked for and configured is done");
        Configure(program, 0, 0, s_both, 3);
        break;
    default:
        CHECK(ModeIs(program, mwin_modeBorderlessFullscreen),
              "fullscreen and maximized states together are fullscreen");
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
    def.size = (mwinSize){640.0f, 480.0f};
    def.mode = mwin_modeMaximized;
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
