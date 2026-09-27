// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's owned and popup windows against the test
// compositor of wayland_server.h with an xdg shell of this test's own:
// an owned window a toplevel whose parent is its owner's; a menu an
// xdg_popup of its owner, offset from the corner of its owner's window
// geometry by its place and its owner's caption, grabbing the seat with
// the latest input's serial, without a frame, and reported where the
// compositor put it; a tooltip grabbing nothing; a menu placed again
// through a reposition, a configure to the same place no move;
// popup_done a close request; and the popups gone
// before their owner. Skipped (exit status 77) without XDG_RUNTIME_DIR
// or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull
#define STEP_NS     3000000000ull
#define ROLES       8

// A positioner's size and offset.
typedef struct Placement
{
    int32_t width;
    int32_t height;
    int32_t x;
    int32_t y;
} Placement;

// An xdg_surface and the role it took: its parent's role, where its
// positioner put it, its grab and window geometry, and when it went,
// counted from 1.
typedef struct Role
{
    struct wl_resource* surface;
    struct wl_resource* xdgSurface;
    struct wl_resource* toplevel;
    struct wl_resource* popup;
    int parent;
    Placement placement;
    uint32_t grabSerial;
    int32_t geometry[4];
    int repositions;
    bool configured;
    int gone;
} Role;

static Server s_server;
static Role s_roles[ROLES];
static int s_roleCount;
static int s_gone;

typedef struct Program
{
    uint64_t startNs;
    uint64_t stepNs;
    int step;
    mwinWindowId ids[4];
    int created;
    bool pressed;
    uint32_t pressSerial;
    int menuMoves;
    mwinPosition menuPlace;
    bool menuClose;
    bool done;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static int RoleOf(const struct wl_resource* resource)
{
    for (int i = 0; i < s_roleCount; i++)
    {
        const Role* role = &s_roles[i];
        if (role->surface == resource || role->xdgSurface == resource ||
            role->toplevel == resource || role->popup == resource)
        {
            return i;
        }
    }
    return -1;
}

// The compositor slides every popup a pixel right of where it was put.
static void ConfigurePopup(Role* role)
{
    const Placement* placement = &role->placement;
    xdg_popup_send_configure(role->popup, placement->x + 1, placement->y, placement->width,
                             placement->height);
    xdg_surface_send_configure(role->xdgSurface, ++s_server.serial);
}

// A role's first commit is answered with its first configure.
static void Commit(struct wl_resource* surface)
{
    int index = RoleOf(surface);
    if (index < 0 || s_roles[index].configured)
    {
        return;
    }
    Role* role = &s_roles[index];
    role->configured = true;
    if (role->popup != nullptr)
    {
        ConfigurePopup(role);
        return;
    }
    struct wl_array states;
    wl_array_init(&states);
    xdg_toplevel_send_configure(role->toplevel, 0, 0, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(role->xdgSurface, ++s_server.serial);
}

static void Gone(struct wl_resource* resource)
{
    int index = RoleOf(resource);
    if (index >= 0 && s_roles[index].gone == 0)
    {
        s_roles[index].gone = ++s_gone;
    }
}

static void NoCode(struct wl_client* client, struct wl_resource* resource, uint32_t code)
{
    (void)client;
    (void)resource;
    (void)code;
}

static void SetParent(struct wl_client* client, struct wl_resource* resource,
                      struct wl_resource* parent)
{
    (void)client;
    s_roles[RoleOf(resource)].parent = parent != nullptr ? RoleOf(parent) : -1;
}

static void NoSeatRequest(struct wl_client* client, struct wl_resource* resource,
                          struct wl_resource* seat, uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
}

static void NoResize(struct wl_client* client, struct wl_resource* resource,
                     struct wl_resource* seat, uint32_t serial, uint32_t edges)
{
    (void)edges;
    NoSeatRequest(client, resource, seat, serial);
}

static void NoMenu(struct wl_client* client, struct wl_resource* resource, struct wl_resource* seat,
                   uint32_t serial, int32_t x, int32_t y)
{
    (void)x;
    (void)y;
    NoSeatRequest(client, resource, seat, serial);
}

static const struct xdg_toplevel_interface s_toplevel = {
    .destroy = ServerDestroyResource,
    .set_parent = SetParent,
    .set_title = ServerNoString,
    .set_app_id = ServerNoString,
    .show_window_menu = NoMenu,
    .move = NoSeatRequest,
    .resize = NoResize,
    .set_max_size = ServerNoTwoInts,
    .set_min_size = ServerNoTwoInts,
    .set_maximized = ServerNoRequest,
    .unset_maximized = ServerNoRequest,
    .set_fullscreen = ServerSetOutput,
    .unset_fullscreen = ServerNoRequest,
    .set_minimized = ServerNoRequest,
};

static void Grab(struct wl_client* client, struct wl_resource* resource, struct wl_resource* seat,
                 uint32_t serial)
{
    (void)client;
    (void)seat;
    s_roles[RoleOf(resource)].grabSerial = serial;
}

static void Reposition(struct wl_client* client, struct wl_resource* resource,
                       struct wl_resource* positioner, uint32_t token)
{
    (void)client;
    Role* role = &s_roles[RoleOf(resource)];
    role->placement = *(const Placement*)wl_resource_get_user_data(positioner);
    role->repositions += 1;
    xdg_popup_send_repositioned(resource, token);
    ConfigurePopup(role);
}

static const struct xdg_popup_interface s_popup = {
    .destroy = ServerDestroyResource,
    .grab = Grab,
    .reposition = Reposition,
};

static void GetToplevel(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    Role* role = &s_roles[RoleOf(resource)];
    role->toplevel =
        wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(role->toplevel, &s_toplevel, nullptr, Gone);
}

static void GetPopup(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                     struct wl_resource* parent, struct wl_resource* positioner)
{
    Role* role = &s_roles[RoleOf(resource)];
    role->parent = parent != nullptr ? RoleOf(parent) : -1;
    role->placement = *(const Placement*)wl_resource_get_user_data(positioner);
    role->popup =
        wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(role->popup, &s_popup, nullptr, Gone);
}

static void Geometry(struct wl_client* client, struct wl_resource* resource, int32_t x, int32_t y,
                     int32_t width, int32_t height)
{
    (void)client;
    int32_t* geometry = s_roles[RoleOf(resource)].geometry;
    geometry[0] = x;
    geometry[1] = y;
    geometry[2] = width;
    geometry[3] = height;
}

static const struct xdg_surface_interface s_xdgSurface = {
    .destroy = ServerDestroyResource,
    .get_toplevel = GetToplevel,
    .get_popup = GetPopup,
    .set_window_geometry = Geometry,
    .ack_configure = NoCode,
};

static void SetSize(struct wl_client* client, struct wl_resource* resource, int32_t width,
                    int32_t height)
{
    (void)client;
    Placement* placement = wl_resource_get_user_data(resource);
    placement->width = width;
    placement->height = height;
}

static void SetOffset(struct wl_client* client, struct wl_resource* resource, int32_t x, int32_t y)
{
    (void)client;
    Placement* placement = wl_resource_get_user_data(resource);
    placement->x = x;
    placement->y = y;
}

static void NoParentSize(struct wl_client* client, struct wl_resource* resource, int32_t width,
                         int32_t height)
{
    ServerNoTwoInts(client, resource, width, height);
}

static const struct xdg_positioner_interface s_positioner = {
    .destroy = ServerDestroyResource,
    .set_size = SetSize,
    .set_anchor_rect = ServerNoRect,
    .set_anchor = NoCode,
    .set_gravity = NoCode,
    .set_constraint_adjustment = NoCode,
    .set_offset = SetOffset,
    .set_reactive = ServerNoRequest,
    .set_parent_size = NoParentSize,
    .set_parent_configure = NoCode,
};

static void FreePositioner(struct wl_resource* resource)
{
    free(wl_resource_get_user_data(resource));
}

static void CreatePositioner(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    struct wl_resource* positioner = wl_resource_create(client, &xdg_positioner_interface,
                                                        wl_resource_get_version(resource), id);
    wl_resource_set_implementation(positioner, &s_positioner, calloc(1, sizeof(Placement)),
                                   FreePositioner);
}

static void GetXdgSurface(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                          struct wl_resource* surface)
{
    Role* role = &s_roles[s_roleCount++];
    *role = (Role){.surface = surface, .parent = -1};
    role->xdgSurface =
        wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(role->xdgSurface, &s_xdgSurface, nullptr, nullptr);
}

static const struct xdg_wm_base_interface s_wmBase = {
    .destroy = ServerDestroyResource,
    .create_positioner = CreatePositioner,
    .get_xdg_surface = GetXdgSurface,
    .pong = NoCode,
};

static void BindWmBase(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    struct wl_resource* resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    wl_resource_set_implementation(resource, &s_wmBase, data, nullptr);
}

static bool Same(mwinWindowId a, mwinWindowId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void Collect(mwinContext* context, Program* program)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->created += event.type == mwin_eventRequestCompleted &&
                            event.data.completion.outcome == mwin_outcomeDone;
        program->pressed = program->pressed || event.type == mwin_eventButtonDown;
        if (event.type == mwin_eventMoved && Same(event.window, program->ids[2]))
        {
            program->menuMoves += 1;
            program->menuPlace = event.data.position;
        }
        program->menuClose = program->menuClose || (event.type == mwin_eventCloseRequested &&
                                                    Same(event.window, program->ids[2]));
    }
}

static mwinWindowId Create(mwinContext* context, mwinWindowId owner, mwinWindowKind kind,
                           mwinPosition position)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){160.0f, 90.0f};
    def.owner = owner;
    def.kind = kind;
    def.position = position;
    mwinWindowId window = {0};
    CHECK(mwinCreateWindow(context, &def, &window, nullptr) == mwin_success, "create");
    return window;
}

// The roles as the compositor has them, taken under its lock.
static void Roles(Role* roles)
{
    pthread_mutex_lock(&s_server.lock);
    memcpy(roles, s_roles, sizeof(s_roles));
    pthread_mutex_unlock(&s_server.lock);
}

static void CheckMade(const Program* program)
{
    Role roles[ROLES];
    Roles(roles);
    int32_t caption = -roles[0].geometry[1];
    CHECK(roles[1].toplevel != nullptr && roles[1].parent == 0,
          "an owned window a toplevel whose parent is its owner's");
    CHECK(roles[2].popup != nullptr && roles[2].parent == 0 && roles[2].placement.x == 10 &&
              roles[2].placement.y == 20 + caption && caption > 0 &&
              roles[2].placement.width == 160 && roles[2].placement.height == 90,
          "a menu a popup of its owner, offset by its place and its owner's caption");
    CHECK(roles[2].grabSerial == program->pressSerial && roles[3].popup != nullptr &&
              roles[3].grabSerial == 0,
          "a menu grabbing with the latest input's serial, a tooltip grabbing nothing");
    CHECK(roles[2].geometry[1] == 0 && roles[2].geometry[3] == 90, "a popup without a frame");
    CHECK(program->menuMoves == 1 && program->menuPlace.x == 11.0f && program->menuPlace.y == 20.0f,
          "a menu reported where the compositor put it");
}

// Whether the step's work is seen yet.
static bool Settled(const Program* program)
{
    Role roles[ROLES];
    Roles(roles);
    switch (program->step)
    {
    case 1:
        return program->pressed;
    case 2:
        return program->created == 4 && program->menuMoves > 0 && roles[2].geometry[2] > 0;
    case 3:
        return program->menuPlace.x == 6.0f && roles[2].repositions == 1;
    case 4:
        return program->menuClose;
    default:
        return roles[0].gone > 0;
    }
}

static void Next(mwinContext* context, Program* program)
{
    mwinWindowState state;
    mwinWindowId owner = program->ids[0];
    Role roles[ROLES];
    switch (program->step)
    {
    case 0:
        ServerPointerEnterOn(&s_server, s_roles[0].surface, 20.0, 20.0);
        ServerButton(&s_server, BTN_RIGHT, true);
        pthread_mutex_lock(&s_server.lock);
        program->pressSerial = s_server.serial;
        pthread_mutex_unlock(&s_server.lock);
        break;
    case 1:
        program->ids[1] = Create(context, owner, mwin_windowNormal, (mwinPosition){0});
        program->ids[2] = Create(context, owner, mwin_windowMenu, (mwinPosition){10, 20});
        program->ids[3] = Create(context, owner, mwin_windowTooltip, (mwinPosition){30, 40});
        break;
    case 2:
        CheckMade(program);
        CHECK(mwinRequestPosition(context, program->ids[2], (mwinPosition){5, 6}, nullptr) ==
                  mwin_success,
              "place the menu");
        break;
    case 3:
        CHECK(mwinGetWindowState(context, program->ids[2], &state) == mwin_success &&
                  state.position.x == 6.0f && state.position.y == 6.0f && program->menuMoves == 2,
              "a menu placed again, reported where the compositor put it");
        pthread_mutex_lock(&s_server.lock);
        // The same place again, which is no move.
        ConfigurePopup(&s_roles[2]);
        xdg_popup_send_popup_done(s_roles[2].popup);
        wl_display_flush_clients(s_server.display);
        pthread_mutex_unlock(&s_server.lock);
        break;
    case 4:
        CHECK(program->menuMoves == 2, "a configure to the same place no move");
        CHECK(mwinDestroyWindow(context, owner) == mwin_success, "destroy the owner");
        break;
    default:
        Roles(roles);
        CHECK(roles[1].gone > 0 && roles[2].gone > 0 && roles[3].gone > 0 &&
                  roles[1].gone < roles[0].gone && roles[2].gone < roles[0].gone &&
                  roles[3].gone < roles[0].gone,
              "owned windows gone before their owner");
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
    return mwinCreateWindow(context, &def, &program->ids[0], nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    static const char* const what[] = {"",
                                       "the press seen",
                                       "the windows made",
                                       "the menu placed again",
                                       "popup_done a close request",
                                       "the owner gone"};
    Program* program = user;
    Collect(context, program);
    if (program->step == 0 && program->created == 1)
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
    return program->done || NowNs() - program->startNs > DEADLINE_NS ? mwin_frameStop
                                                                     : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&s_server, "us", ""))
    {
        return 77;
    }
    pthread_mutex_lock(&s_server.lock);
    wl_global_destroy(s_server.shellGlobal);
    wl_global_create(s_server.display, &xdg_wm_base_interface, 3, nullptr, BindWmBase);
    s_server.commit = Commit;
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
