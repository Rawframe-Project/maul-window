// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's custom chrome against the test compositor of
// wayland_server.h:
// - a decorated window of custom chrome asks the compositor for
//   decorations of the client's, and gets no frame of the backend's;
// - a left press on a caption moves the window and one on an edge
//   resizes it from that edge, through the compositor, and the program
//   is told of neither nor of their releases;
// - a press on a maximize button is the program's;
// - a double click on a caption maximizes the window;
// - a right press on a caption opens the window menu.
// Skipped (exit status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <time.h>
#include <xdg-decoration-unstable-v1-server-protocol.h>

#define DEADLINE_NS 10000000000ull
#define STEP_NS     2000000000ull

static Server s_server;
// The decoration mode the client asked for, 0 before it did.
static uint32_t s_mode;

typedef struct Program
{
    uint64_t startNs;
    uint64_t stepNs;
    int step;
    mwinWindowId window;
    int completions;
    int downs;
    int ups;
    bool done;
} Program;

static const mwinHitRegion s_regions[3] = {
    {{0.0f, 0.0f, 400.0f, 30.0f}, mwin_hitCaption},
    {{340.0f, 0.0f, 30.0f, 30.0f}, mwin_hitMaximize},
    {{0.0f, 30.0f, 4.0f, 270.0f}, mwin_hitLeft},
};

// The compositor takes the mode the client asks for.
static void SetMode(struct wl_client* client, struct wl_resource* resource, uint32_t mode)
{
    (void)client;
    s_mode = mode;
    zxdg_toplevel_decoration_v1_send_configure(resource, mode);
}

static void UnsetMode(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    (void)resource;
}

static const struct zxdg_toplevel_decoration_v1_interface s_decoration = {ServerDestroyResource,
                                                                          SetMode, UnsetMode};

static void GetDecoration(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                          struct wl_resource* toplevel)
{
    (void)toplevel;
    struct wl_resource* decoration = wl_resource_create(
        client, &zxdg_toplevel_decoration_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(decoration, &s_decoration, nullptr, nullptr);
}

static const struct zxdg_decoration_manager_v1_interface s_decorations = {ServerDestroyResource,
                                                                          GetDecoration};

static void BindDecorations(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &zxdg_decoration_manager_v1_interface, (int)version, id);
    wl_resource_set_implementation(resource, &s_decorations, data, nullptr);
}

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Collect(mwinContext* context, Program* program)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->completions += event.type == mwin_eventRequestCompleted &&
                                event.data.completion.outcome == mwin_outcomeDone;
        program->downs += event.type == mwin_eventButtonDown;
        program->ups += event.type == mwin_eventButtonUp;
    }
}

// Clicks a button at a point of the content, in logical units.
static void Click(double x, double y, uint32_t button)
{
    ServerPointerMotion(&s_server, x, y);
    ServerButton(&s_server, button, true);
    ServerButton(&s_server, button, false);
}

// Whether the step's work is seen yet.
static bool Settled(const Program* program)
{
    Shell shell = ServerShell(&s_server);
    switch (program->step)
    {
    case 1:
        return program->completions == 2;
    case 2:
        return shell.moves == 1;
    case 3:
        return shell.resizeEdge == XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    case 4:
        return program->downs == 1 && program->ups == 1;
    case 5:
        return shell.maximizes == 1;
    default:
        return shell.menus == 1;
    }
}

static void Next(mwinContext* context, Program* program)
{
    Shell shell = ServerShell(&s_server);
    switch (program->step)
    {
    case 0:
        CHECK(mwinRequestHitRegions(context, program->window, s_regions, 3, nullptr) ==
                  mwin_success,
              "regions");
        ServerPointerEnter(&s_server, 200.0, 150.0);
        break;
    case 1:
        Click(100.0, 10.0, BTN_LEFT);
        break;
    case 2:
        pthread_mutex_lock(&s_server.lock);
        CHECK(s_mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
              "a decorated window of custom chrome asks for the client's decorations");
        pthread_mutex_unlock(&s_server.lock);
        CHECK(shell.parts == 0 && shell.geometry[1] == 0 && shell.geometry[3] == 300,
              "and gets no frame of the backend's");
        CHECK(program->downs == 0 && program->ups == 0,
              "a press on a caption the compositor's move, not the program's");
        Click(2.0, 100.0, BTN_LEFT);
        break;
    case 3:
        CHECK(shell.moves == 1 && program->downs == 0 && program->ups == 0,
              "a press on the left edge the compositor's resize from it");
        Click(350.0, 10.0, BTN_LEFT);
        break;
    case 4:
        Click(100.0, 10.0, BTN_LEFT);
        Click(100.0, 10.0, BTN_LEFT);
        break;
    case 5:
        CHECK(shell.moves == 2, "the first click of the two a move");
        Click(100.0, 10.0, BTN_RIGHT);
        break;
    default:
        CHECK(program->downs == 1 && program->ups == 1,
              "the program told of the maximize button's press alone");
        program->done = true;
        break;
    }
    program->step += 1;
    program->stepNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){400.0f, 300.0f};
    def.style = mwin_styleResizable | mwin_styleDecorated | mwin_styleCustomChrome;
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    static const char* const what[] = {
        "",
        "the regions taken",
        "the caption's move",
        "the resize",
        "the maximize button's press and release",
        "the double click's maximize",
        "the window menu",
    };
    Program* program = user;
    Collect(context, program);
    if (program->step == 0 && program->completions == 1)
    {
        Next(context, program);
    }
    else if (program->step > 0 && !program->done)
    {
        bool settled = Settled(program);
        if (settled || NowNs() - program->stepNs > STEP_NS)
        {
            CHECK(settled, what[program->step]);
            Next(context, program);
        }
    }
    struct timespec pause = {0, 500000};
    (void)nanosleep(&pause, nullptr);
    bool late = NowNs() - program->startNs > DEADLINE_NS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&s_server, "us", ""))
    {
        return 77;
    }
    pthread_mutex_lock(&s_server.lock);
    wl_global_create(s_server.display, &zxdg_decoration_manager_v1_interface, 1, nullptr,
                     BindDecorations);
    pthread_mutex_unlock(&s_server.lock);
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs on the test compositor");
    ServerStop(&s_server);
    return s_failures == 0 ? 0 : 1;
}
