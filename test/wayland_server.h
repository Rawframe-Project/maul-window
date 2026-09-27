// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A small Wayland compositor for the backend's input tests, on
// libwayland-server in a thread of the test: one output, xdg-shell
// toplevels that are configured activated at their first commit, and a
// seat with a keyboard whose keymap is compiled from RMLVO names. The
// test drives the seat through the Server functions, which take the
// server's lock; the thread dispatches the clients' requests between
// them.

#ifndef MAUL_WINDOW_TEST_WAYLAND_SERVER_H
#define MAUL_WINDOW_TEST_WAYLAND_SERVER_H

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server.h>
#include <xdg-shell-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

typedef struct Server
{
    struct wl_display* display;
    struct wl_event_loop* loop;
    pthread_t thread;
    pthread_mutex_t lock;
    bool stop;
    const char* socket;
    // The keymap as text, and the last surface and keyboard made.
    char* keymap;
    size_t keymapSize;
    struct wl_resource* surface;
    struct wl_resource* toplevel;
    struct wl_resource* xdgSurface;
    struct wl_resource* keyboard;
    struct wl_resource* pointer;
    struct wl_resource* touch;
    int32_t repeatRate;
    int32_t repeatDelay;
    uint32_t serial;
    bool configured;
} Server;

static void ServerDestroyResource(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void ServerNoAttach(struct wl_client* client, struct wl_resource* resource,
                           struct wl_resource* buffer, int32_t x, int32_t y)
{
    (void)client;
    (void)resource;
    (void)buffer;
    (void)x;
    (void)y;
}

static void ServerNoRect(struct wl_client* client, struct wl_resource* resource, int32_t x,
                         int32_t y, int32_t width, int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void ServerNoInt(struct wl_client* client, struct wl_resource* resource, int32_t value)
{
    (void)client;
    (void)resource;
    (void)value;
}

static void ServerNoTwoInts(struct wl_client* client, struct wl_resource* resource, int32_t a,
                            int32_t b)
{
    (void)client;
    (void)resource;
    (void)a;
    (void)b;
}

static void ServerNoString(struct wl_client* client, struct wl_resource* resource, const char* text)
{
    (void)client;
    (void)resource;
    (void)text;
}

static void ServerNoRequest(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    (void)resource;
}

// The first commit of a toplevel is answered with its first configure.
static void ServerCommit(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    if (server->toplevel == nullptr || server->configured)
    {
        return;
    }
    server->configured = true;
    struct wl_array states;
    wl_array_init(&states);
    uint32_t* state = wl_array_add(&states, sizeof(uint32_t));
    *state = XDG_TOPLEVEL_STATE_ACTIVATED;
    xdg_toplevel_send_configure(server->toplevel, 0, 0, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(server->xdgSurface, ++server->serial);
}

static const struct wl_surface_interface s_serverSurface = {
    .destroy = ServerDestroyResource,
    .attach = ServerNoAttach,
    .damage = ServerNoRect,
    .commit = ServerCommit,
    .set_buffer_transform = ServerNoInt,
    .set_buffer_scale = ServerNoInt,
    .damage_buffer = ServerNoRect,
    .offset = ServerNoTwoInts,
};

static void ServerCreateSurface(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    struct wl_resource* surface =
        wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(surface, &s_serverSurface, server, nullptr);
    server->surface = surface;
}

static const struct wl_compositor_interface s_serverCompositor = {
    .create_surface = ServerCreateSurface,
};

static void ServerBindCompositor(struct wl_client* client, void* data, uint32_t version,
                                 uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &wl_compositor_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverCompositor, data, nullptr);
}

static void ServerSetOutput(struct wl_client* client, struct wl_resource* resource,
                            struct wl_resource* output)
{
    (void)client;
    (void)resource;
    (void)output;
}

static const struct xdg_toplevel_interface s_serverToplevel = {
    .destroy = ServerDestroyResource,
    .set_title = ServerNoString,
    .set_app_id = ServerNoString,
    .set_max_size = ServerNoTwoInts,
    .set_min_size = ServerNoTwoInts,
    .set_maximized = ServerNoRequest,
    .unset_maximized = ServerNoRequest,
    .set_fullscreen = ServerSetOutput,
    .unset_fullscreen = ServerNoRequest,
    .set_minimized = ServerNoRequest,
};

static void ServerGetToplevel(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    server->toplevel =
        wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->toplevel, &s_serverToplevel, server, nullptr);
    server->configured = false;
}

static void ServerAck(struct wl_client* client, struct wl_resource* resource, uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)serial;
}

static const struct xdg_surface_interface s_serverXdgSurface = {
    .destroy = ServerDestroyResource,
    .get_toplevel = ServerGetToplevel,
    .set_window_geometry = ServerNoRect,
    .ack_configure = ServerAck,
};

static void ServerGetXdgSurface(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                                struct wl_resource* surface)
{
    (void)surface;
    Server* server = wl_resource_get_user_data(resource);
    server->xdgSurface =
        wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->xdgSurface, &s_serverXdgSurface, server, nullptr);
}

static void ServerPong(struct wl_client* client, struct wl_resource* resource, uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)serial;
}

static const struct xdg_wm_base_interface s_serverWmBase = {
    .destroy = ServerDestroyResource,
    .get_xdg_surface = ServerGetXdgSurface,
    .pong = ServerPong,
};

static void ServerBindWmBase(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    struct wl_resource* resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverWmBase, data, nullptr);
}

static const struct wl_keyboard_interface s_serverKeyboard = {
    .release = ServerDestroyResource,
};

// Sends the keymap through a sealed memory file, as compositors do.
static void ServerSendKeymap(Server* server)
{
    int fd = memfd_create("keymap", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)server->keymapSize) != 0)
    {
        return;
    }
    void* map = mmap(nullptr, server->keymapSize, PROT_WRITE, MAP_SHARED, fd, 0);
    memcpy(map, server->keymap, server->keymapSize);
    munmap(map, server->keymapSize);
    wl_keyboard_send_keymap(server->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                            (uint32_t)server->keymapSize);
    close(fd);
}

static void ServerGetKeyboard(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    server->keyboard =
        wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->keyboard, &s_serverKeyboard, server, nullptr);
    ServerSendKeymap(server);
    wl_keyboard_send_repeat_info(server->keyboard, server->repeatRate, server->repeatDelay);
}

static void ServerSetCursor(struct wl_client* client, struct wl_resource* resource, uint32_t serial,
                            struct wl_resource* surface, int32_t x, int32_t y)
{
    (void)client;
    (void)resource;
    (void)serial;
    (void)surface;
    (void)x;
    (void)y;
}

static const struct wl_pointer_interface s_serverPointer = {
    .set_cursor = ServerSetCursor,
    .release = ServerDestroyResource,
};

static void ServerGetPointer(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    server->pointer =
        wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->pointer, &s_serverPointer, server, nullptr);
}

static const struct wl_touch_interface s_serverTouch = {
    .release = ServerDestroyResource,
};

static void ServerGetTouch(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    server->touch =
        wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->touch, &s_serverTouch, server, nullptr);
}

static const struct wl_seat_interface s_serverSeat = {
    .get_pointer = ServerGetPointer,
    .get_keyboard = ServerGetKeyboard,
    .get_touch = ServerGetTouch,
    .release = ServerDestroyResource,
};

static void ServerBindSeat(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    struct wl_resource* resource = wl_resource_create(client, &wl_seat_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverSeat, data, nullptr);
    wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER |
                                            WL_SEAT_CAPABILITY_TOUCH);
}

static void* ServerRun(void* data)
{
    Server* server = data;
    for (;;)
    {
        pthread_mutex_lock(&server->lock);
        bool stop = server->stop;
        if (!stop)
        {
            wl_event_loop_dispatch(server->loop, 0);
            wl_display_flush_clients(server->display);
        }
        pthread_mutex_unlock(&server->lock);
        if (stop)
        {
            return nullptr;
        }
        struct timespec pause = {0, 200000};
        (void)nanosleep(&pause, nullptr);
    }
}

// Compiles the keymap from RMLVO names. False without xkb data.
static bool ServerCompileKeymap(Server* server, const char* layout, const char* variant)
{
    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names names = {"evdev", "pc105", layout, variant, nullptr};
    struct xkb_keymap* keymap =
        context != nullptr ? xkb_keymap_new_from_names(context, &names, 0) : nullptr;
    if (keymap != nullptr)
    {
        server->keymap = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
        server->keymapSize = strlen(server->keymap) + 1;
        xkb_keymap_unref(keymap);
    }
    if (context != nullptr)
    {
        xkb_context_unref(context);
    }
    return server->keymap != nullptr;
}

// Starts the compositor on a new socket, which WAYLAND_DISPLAY then
// names. False when it cannot.
static bool ServerStart(Server* server, const char* layout, const char* variant)
{
    *server = (Server){.repeatRate = 50, .repeatDelay = 40};
    if (!ServerCompileKeymap(server, layout, variant))
    {
        return false;
    }
    server->display = wl_display_create();
    server->socket = wl_display_add_socket_auto(server->display);
    if (server->socket == nullptr)
    {
        return false;
    }
    server->loop = wl_display_get_event_loop(server->display);
    wl_global_create(server->display, &wl_compositor_interface, 4, server, ServerBindCompositor);
    wl_global_create(server->display, &xdg_wm_base_interface, 5, server, ServerBindWmBase);
    wl_global_create(server->display, &wl_seat_interface, 8, server, ServerBindSeat);
    setenv("WAYLAND_DISPLAY", server->socket, 1);
    pthread_mutex_init(&server->lock, nullptr);
    return pthread_create(&server->thread, nullptr, ServerRun, server) == 0;
}

static void ServerStop(Server* server)
{
    pthread_mutex_lock(&server->lock);
    server->stop = true;
    pthread_mutex_unlock(&server->lock);
    pthread_join(server->thread, nullptr);
    wl_display_destroy_clients(server->display);
    wl_display_destroy(server->display);
    pthread_mutex_destroy(&server->lock);
    free(server->keymap);
}

// The seat's keyboard focus comes to the last surface made.
static void ServerEnter(Server* server)
{
    pthread_mutex_lock(&server->lock);
    struct wl_array keys;
    wl_array_init(&keys);
    wl_keyboard_send_enter(server->keyboard, ++server->serial, server->surface, &keys);
    wl_keyboard_send_modifiers(server->keyboard, ++server->serial, 0, 0, 0, 0);
    wl_array_release(&keys);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static void ServerLeave(Server* server)
{
    pthread_mutex_lock(&server->lock);
    wl_keyboard_send_leave(server->keyboard, ++server->serial, server->surface);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// A key by its evdev code, and the modifier state and layout group.
static void ServerKey(Server* server, uint32_t evdev, bool pressed)
{
    pthread_mutex_lock(&server->lock);
    wl_keyboard_send_key(server->keyboard, ++server->serial, 1000, evdev,
                         pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static void ServerModifiers(Server* server, uint32_t depressed, uint32_t group)
{
    pthread_mutex_lock(&server->lock);
    wl_keyboard_send_modifiers(server->keyboard, ++server->serial, depressed, 0, 0, group);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// Pointer events, each in a frame of its own unless noted.
static void ServerPointerEnter(Server* server, double x, double y)
{
    pthread_mutex_lock(&server->lock);
    wl_pointer_send_enter(server->pointer, ++server->serial, server->surface,
                          wl_fixed_from_double(x), wl_fixed_from_double(y));
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static void ServerPointerLeave(Server* server)
{
    pthread_mutex_lock(&server->lock);
    wl_pointer_send_leave(server->pointer, ++server->serial, server->surface);
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// Two motions in one frame, of which the program sees the last.
static void ServerPointerMotion(Server* server, double x, double y)
{
    pthread_mutex_lock(&server->lock);
    wl_pointer_send_motion(server->pointer, 1000, wl_fixed_from_double(x - 5.0),
                           wl_fixed_from_double(y - 5.0));
    wl_pointer_send_motion(server->pointer, 1000, wl_fixed_from_double(x), wl_fixed_from_double(y));
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static void ServerButton(Server* server, uint32_t button, bool pressed)
{
    pthread_mutex_lock(&server->lock);
    wl_pointer_send_button(server->pointer, ++server->serial, 1000, button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                   : WL_POINTER_BUTTON_STATE_RELEASED);
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// A wheel turn toward the user by steps120 120ths of a detent, with a
// continuous distance of as many units, which the program must not
// count as well.
static void ServerWheel(Server* server, int32_t steps120)
{
    pthread_mutex_lock(&server->lock);
    wl_pointer_send_axis_source(server->pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
    wl_pointer_send_axis_value120(server->pointer, WL_POINTER_AXIS_VERTICAL_SCROLL, steps120);
    wl_pointer_send_axis(server->pointer, 1000, WL_POINTER_AXIS_VERTICAL_SCROLL,
                         wl_fixed_from_int(steps120));
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// A touch goes down, moves and lifts.
static void ServerTouchStroke(Server* server, int32_t id)
{
    pthread_mutex_lock(&server->lock);
    wl_touch_send_down(server->touch, ++server->serial, 1000, server->surface, id,
                       wl_fixed_from_int(1), wl_fixed_from_int(2));
    wl_touch_send_frame(server->touch);
    wl_touch_send_motion(server->touch, 1000, id, wl_fixed_from_int(3), wl_fixed_from_int(4));
    wl_touch_send_frame(server->touch);
    wl_touch_send_up(server->touch, ++server->serial, 1000, id);
    wl_touch_send_frame(server->touch);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

#endif // MAUL_WINDOW_TEST_WAYLAND_SERVER_H
