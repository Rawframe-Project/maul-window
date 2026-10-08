// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tablet manager of the test compositor (wayland_server.h): the
// client's tablet seat gets a tablet, a pen (tilt and pressure) and an
// eraser (pressure); a test moves the tools near the window's surface,
// presses, lifts and clicks them, each change closed by a frame, and
// reads the cursors the client set for them.

#ifndef MAUL_WINDOW_TEST_WAYLAND_TABLET_SERVER_H
#define MAUL_WINDOW_TEST_WAYLAND_TABLET_SERVER_H

#include "wayland_server.h"

#include <tablet-unstable-v2-server-protocol.h>

typedef enum TabletTool
{
    tabletPen,
    tabletEraser,
    tabletTools,
} TabletTool;

typedef struct TabletServer
{
    Server* server;
    struct wl_global* global;
    struct wl_resource* tablet;
    struct wl_resource* tools[tabletTools];
    // set_cursor calls on any tool: with a surface, and without.
    int cursorSurfaces;
    int cursorHides;
} TabletServer;

// What a tool does in one frame; a negative pressure sends none, a NAN
// tilt none.
typedef struct TabletChange
{
    bool near;
    bool left;
    bool down;
    bool up;
    bool moves;
    double x;
    double y;
    int32_t pressure;
    double tiltX;
    double tiltY;
    // A button's code and whether it is pressed, 0 for none.
    uint32_t button;
    bool pressed;
} TabletChange;

static inline void TabletSetCursor(struct wl_client* client, struct wl_resource* resource,
                                   uint32_t serial, struct wl_resource* surface, int32_t x,
                                   int32_t y)
{
    (void)client;
    (void)serial;
    (void)x;
    (void)y;
    TabletServer* tablet = wl_resource_get_user_data(resource);
    tablet->cursorSurfaces += surface != nullptr ? 1 : 0;
    tablet->cursorHides += surface == nullptr ? 1 : 0;
}

static const struct zwp_tablet_tool_v2_interface s_tabletTool = {
    TabletSetCursor,
    ServerDestroyResource,
};

static const struct zwp_tablet_v2_interface s_tabletTablet = {ServerDestroyResource};

static const struct zwp_tablet_seat_v2_interface s_tabletSeat = {ServerDestroyResource};

static inline void TabletForgetTool(struct wl_resource* resource)
{
    TabletServer* tablet = wl_resource_get_user_data(resource);
    for (int i = 0; i < tabletTools; i++)
    {
        tablet->tools[i] = tablet->tools[i] == resource ? nullptr : tablet->tools[i];
    }
}

static inline void TabletForgetTablet(struct wl_resource* resource)
{
    TabletServer* tablet = wl_resource_get_user_data(resource);
    tablet->tablet = tablet->tablet == resource ? nullptr : tablet->tablet;
}

static inline void TabletAddTool(TabletServer* tablet, struct wl_resource* seat, TabletTool which)
{
    struct wl_client* client = wl_resource_get_client(seat);
    struct wl_resource* tool = wl_resource_create(client, &zwp_tablet_tool_v2_interface, 1, 0);
    wl_resource_set_implementation(tool, &s_tabletTool, tablet, TabletForgetTool);
    tablet->tools[which] = tool;
    zwp_tablet_seat_v2_send_tool_added(seat, tool);
    zwp_tablet_tool_v2_send_type(tool, which == tabletPen ? ZWP_TABLET_TOOL_V2_TYPE_PEN
                                                          : ZWP_TABLET_TOOL_V2_TYPE_ERASER);
    if (which == tabletPen)
    {
        zwp_tablet_tool_v2_send_capability(tool, ZWP_TABLET_TOOL_V2_CAPABILITY_TILT);
    }
    zwp_tablet_tool_v2_send_capability(tool, ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE);
    zwp_tablet_tool_v2_send_done(tool);
}

static inline void TabletGetSeat(struct wl_client* client, struct wl_resource* resource,
                                 uint32_t id, struct wl_resource* wlSeat)
{
    (void)wlSeat;
    TabletServer* tablet = wl_resource_get_user_data(resource);
    struct wl_resource* seat = wl_resource_create(client, &zwp_tablet_seat_v2_interface, 1, id);
    wl_resource_set_implementation(seat, &s_tabletSeat, tablet, nullptr);
    tablet->tablet = wl_resource_create(client, &zwp_tablet_v2_interface, 1, 0);
    wl_resource_set_implementation(tablet->tablet, &s_tabletTablet, tablet, TabletForgetTablet);
    zwp_tablet_seat_v2_send_tablet_added(seat, tablet->tablet);
    zwp_tablet_v2_send_name(tablet->tablet, "Maul test tablet");
    zwp_tablet_v2_send_done(tablet->tablet);
    TabletAddTool(tablet, seat, tabletPen);
    TabletAddTool(tablet, seat, tabletEraser);
}

static const struct zwp_tablet_manager_v2_interface s_tabletManager = {
    TabletGetSeat,
    ServerDestroyResource,
};

static inline void TabletBind(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    (void)version;
    struct wl_resource* resource =
        wl_resource_create(client, &zwp_tablet_manager_v2_interface, 1, id);
    wl_resource_set_implementation(resource, &s_tabletManager, data, nullptr);
}

// Offers the tablet manager.
static inline void TabletAdd(TabletServer* tablet, Server* server)
{
    pthread_mutex_lock(&server->lock);
    *tablet = (TabletServer){.server = server};
    tablet->global =
        wl_global_create(server->display, &zwp_tablet_manager_v2_interface, 1, tablet, TabletBind);
    pthread_mutex_unlock(&server->lock);
}

// A tool's changes over the window's surface, closed by a frame.
static inline void TabletSend(TabletServer* tablet, TabletTool which, TabletChange change)
{
    Server* server = tablet->server;
    pthread_mutex_lock(&server->lock);
    struct wl_resource* tool = tablet->tools[which];
    if (change.near)
    {
        zwp_tablet_tool_v2_send_proximity_in(tool, ++server->serial, tablet->tablet,
                                             server->surface);
    }
    if (change.down)
    {
        zwp_tablet_tool_v2_send_down(tool, ++server->serial);
    }
    if (change.up)
    {
        zwp_tablet_tool_v2_send_up(tool);
    }
    if (change.moves)
    {
        zwp_tablet_tool_v2_send_motion(tool, wl_fixed_from_double(change.x),
                                       wl_fixed_from_double(change.y));
    }
    if (change.pressure >= 0)
    {
        zwp_tablet_tool_v2_send_pressure(tool, (uint32_t)change.pressure);
    }
    if (change.tiltX == change.tiltX)
    {
        zwp_tablet_tool_v2_send_tilt(tool, wl_fixed_from_double(change.tiltX),
                                     wl_fixed_from_double(change.tiltY));
    }
    if (change.button != 0)
    {
        zwp_tablet_tool_v2_send_button(tool, ++server->serial, change.button,
                                       change.pressed ? ZWP_TABLET_TOOL_V2_BUTTON_STATE_PRESSED
                                                      : ZWP_TABLET_TOOL_V2_BUTTON_STATE_RELEASED);
    }
    if (change.left)
    {
        zwp_tablet_tool_v2_send_proximity_out(tool);
    }
    zwp_tablet_tool_v2_send_frame(tool, 1000);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

#endif // MAUL_WINDOW_TEST_WAYLAND_TABLET_SERVER_H
