// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A small Wayland compositor for the backend's input tests, on
// libwayland-server in a thread of the test: xdg-shell toplevels that
// are configured activated at their first commit; a seat with a
// keyboard whose keymap is compiled from RMLVO names, a pointer and a
// touch screen; subsurfaces and shared memory, with the shell requests
// a client's own frame makes; cursor shapes, cursor surfaces with the
// size, buffer scale and first pixel of their shared memory and, for a
// test that adds the viewporter, their viewport's destination; pointer
// constraints and relative motion, with what the client asked for kept
// in the Cursor state; and a text input whose committed state is kept
// in TextState. The test drives the seat through the Server functions,
// which take the server's lock; the thread dispatches the clients'
// requests between them.

#ifndef MAUL_WINDOW_TEST_WAYLAND_SERVER_H
#define MAUL_WINDOW_TEST_WAYLAND_SERVER_H

#include <cursor-shape-v1-server-protocol.h>
#include <pointer-constraints-unstable-v1-server-protocol.h>
#include <pthread.h>
#include <relative-pointer-unstable-v1-server-protocol.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <text-input-unstable-v3-server-protocol.h>
#include <unistd.h>
#include <viewporter-server-protocol.h>
#include <wayland-server.h>
#include <xdg-shell-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

// What the client last asked for of the pointer's cursor.
typedef struct Cursor
{
    // wp_cursor_shape_device_v1 shape, 0 before any.
    uint32_t shape;
    // set_cursor calls with no surface, which hide it.
    int hides;
    bool locked;
    bool confined;
    // set_cursor calls with a surface, and the last one's hotspot and
    // what its surface held: buffer size, buffer scale, first pixel and
    // viewport destination (0 for none); and whether a surface is what
    // shows, not a shape or nothing.
    int images;
    int32_t hotspotX;
    int32_t hotspotY;
    int32_t width;
    int32_t height;
    int32_t scale;
    uint32_t pixel;
    int32_t destinationWidth;
    int32_t destinationHeight;
    bool surfaceShown;
} Cursor;

// What a surface was last given: an attached shared memory buffer's
// size and first pixel, its buffer scale and its viewport destination.
typedef struct SurfaceState
{
    struct wl_resource* surface;
    int32_t width;
    int32_t height;
    int32_t scale;
    uint32_t pixel;
    int32_t destinationWidth;
    int32_t destinationHeight;
} SurfaceState;

#define SERVER_SURFACES 16

// The text input state the client last committed.
typedef struct TextState
{
    bool enabled;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t commits;
} TextState;

// What the client asked of its toplevel's shell: the window geometry,
// and the moves, resizes, window menus and maximizations it started.
typedef struct Shell
{
    int32_t geometry[4];
    int moves;
    uint32_t resizeEdge;
    int menus;
    int maximizes;
    // The subsurfaces made, with their surfaces and places.
    int parts;
} Shell;

#define SERVER_PARTS 8

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
    struct wl_resource* relative;
    Cursor cursor;
    SurfaceState surfaces[SERVER_SURFACES];
    struct wl_resource* textInput;
    // What the client asked for since its last commit, and what holds.
    TextState pendingText;
    TextState text;
    Shell shell;
    struct
    {
        struct wl_resource* surface;
        struct wl_resource* subsurface;
        int32_t x;
        int32_t y;
    } parts[SERVER_PARTS];
    int32_t repeatRate;
    int32_t repeatDelay;
    uint32_t serial;
    bool configured;
    // The xdg-shell global, which a test with a shell of its own
    // replaces, and a test's hook on every surface commit, or null.
    struct wl_global* shellGlobal;
    void (*commit)(struct wl_resource* surface);
} Server;

static inline void ServerDestroyResource(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

// A surface's state, made on first use; NULL past SERVER_SURFACES.
static inline SurfaceState* ServerSurfaceState(Server* server, struct wl_resource* surface)
{
    for (int i = 0; i < SERVER_SURFACES; i++)
    {
        if (server->surfaces[i].surface == nullptr)
        {
            server->surfaces[i] = (SurfaceState){.surface = surface, .scale = 1};
        }
        if (server->surfaces[i].surface == surface)
        {
            return &server->surfaces[i];
        }
    }
    return nullptr;
}

static inline void ServerAttach(struct wl_client* client, struct wl_resource* resource,
                                struct wl_resource* buffer, int32_t x, int32_t y)
{
    (void)client;
    (void)x;
    (void)y;
    SurfaceState* state = ServerSurfaceState(wl_resource_get_user_data(resource), resource);
    struct wl_shm_buffer* shm = buffer != nullptr ? wl_shm_buffer_get(buffer) : nullptr;
    if (state == nullptr || shm == nullptr)
    {
        return;
    }
    wl_shm_buffer_begin_access(shm);
    state->width = wl_shm_buffer_get_width(shm);
    state->height = wl_shm_buffer_get_height(shm);
    memcpy(&state->pixel, wl_shm_buffer_get_data(shm), sizeof(state->pixel));
    wl_shm_buffer_end_access(shm);
}

static inline void ServerSetBufferScale(struct wl_client* client, struct wl_resource* resource,
                                        int32_t scale)
{
    (void)client;
    SurfaceState* state = ServerSurfaceState(wl_resource_get_user_data(resource), resource);
    if (state != nullptr)
    {
        state->scale = scale;
    }
}

static inline void ServerNoRect(struct wl_client* client, struct wl_resource* resource, int32_t x,
                                int32_t y, int32_t width, int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static inline void ServerNoInt(struct wl_client* client, struct wl_resource* resource,
                               int32_t value)
{
    (void)client;
    (void)resource;
    (void)value;
}

static inline void ServerNoTwoInts(struct wl_client* client, struct wl_resource* resource,
                                   int32_t a, int32_t b)
{
    (void)client;
    (void)resource;
    (void)a;
    (void)b;
}

static inline void ServerNoString(struct wl_client* client, struct wl_resource* resource,
                                  const char* text)
{
    (void)client;
    (void)resource;
    (void)text;
}

static inline void ServerNoRequest(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    (void)resource;
}

// The first commit of a toplevel is answered with its first configure.
static inline void ServerCommit(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    if (server->commit != nullptr)
    {
        server->commit(resource);
    }
    if (server->toplevel == nullptr || server->configured || resource != server->surface)
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
    .attach = ServerAttach,
    .damage = ServerNoRect,
    .commit = ServerCommit,
    .set_buffer_transform = ServerNoInt,
    .set_buffer_scale = ServerSetBufferScale,
    .damage_buffer = ServerNoRect,
    .offset = ServerNoTwoInts,
};

static inline void ServerCreateSurface(struct wl_client* client, struct wl_resource* resource,
                                       uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    struct wl_resource* surface =
        wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(surface, &s_serverSurface, server, nullptr);
}

static const struct wl_compositor_interface s_serverCompositor = {
    .create_surface = ServerCreateSurface,
};

static inline void ServerBindCompositor(struct wl_client* client, void* data, uint32_t version,
                                        uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &wl_compositor_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverCompositor, data, nullptr);
}

static inline void ServerSetOutput(struct wl_client* client, struct wl_resource* resource,
                                   struct wl_resource* output)
{
    (void)client;
    (void)resource;
    (void)output;
}

static inline void ServerMove(struct wl_client* client, struct wl_resource* resource,
                              struct wl_resource* seat, uint32_t serial)
{
    (void)client;
    (void)seat;
    (void)serial;
    Server* server = wl_resource_get_user_data(resource);
    server->shell.moves += 1;
}

static inline void ServerResize(struct wl_client* client, struct wl_resource* resource,
                                struct wl_resource* seat, uint32_t serial, uint32_t edges)
{
    (void)client;
    (void)seat;
    (void)serial;
    Server* server = wl_resource_get_user_data(resource);
    server->shell.resizeEdge = edges;
}

static inline void ServerMenu(struct wl_client* client, struct wl_resource* resource,
                              struct wl_resource* seat, uint32_t serial, int32_t x, int32_t y)
{
    (void)client;
    (void)seat;
    (void)serial;
    (void)x;
    (void)y;
    Server* server = wl_resource_get_user_data(resource);
    server->shell.menus += 1;
}

static inline void ServerMaximize(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    server->shell.maximizes += 1;
}

static const struct xdg_toplevel_interface s_serverToplevel = {
    .destroy = ServerDestroyResource,
    .set_title = ServerNoString,
    .set_app_id = ServerNoString,
    .set_max_size = ServerNoTwoInts,
    .set_min_size = ServerNoTwoInts,
    .show_window_menu = ServerMenu,
    .move = ServerMove,
    .resize = ServerResize,
    .set_maximized = ServerMaximize,
    .unset_maximized = ServerNoRequest,
    .set_fullscreen = ServerSetOutput,
    .unset_fullscreen = ServerNoRequest,
    .set_minimized = ServerNoRequest,
};

static inline void ServerGetToplevel(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    server->toplevel =
        wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->toplevel, &s_serverToplevel, server, nullptr);
    server->configured = false;
}

static inline void ServerAck(struct wl_client* client, struct wl_resource* resource,
                             uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)serial;
}

static inline void ServerGeometry(struct wl_client* client, struct wl_resource* resource, int32_t x,
                                  int32_t y, int32_t width, int32_t height)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    server->shell.geometry[0] = x;
    server->shell.geometry[1] = y;
    server->shell.geometry[2] = width;
    server->shell.geometry[3] = height;
}

static const struct xdg_surface_interface s_serverXdgSurface = {
    .destroy = ServerDestroyResource,
    .get_toplevel = ServerGetToplevel,
    .set_window_geometry = ServerGeometry,
    .ack_configure = ServerAck,
};

static inline void ServerGetXdgSurface(struct wl_client* client, struct wl_resource* resource,
                                       uint32_t id, struct wl_resource* surface)
{
    Server* server = wl_resource_get_user_data(resource);
    server->surface = surface;
    server->xdgSurface =
        wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->xdgSurface, &s_serverXdgSurface, server, nullptr);
}

static inline void ServerPong(struct wl_client* client, struct wl_resource* resource,
                              uint32_t serial)
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

static inline void ServerBindWmBase(struct wl_client* client, void* data, uint32_t version,
                                    uint32_t id)
{
    struct wl_resource* resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverWmBase, data, nullptr);
}

static const struct wl_keyboard_interface s_serverKeyboard = {
    .release = ServerDestroyResource,
};

// Sends the keymap through a sealed memory file, as compositors do.
static inline void ServerSendKeymap(Server* server)
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

static inline void ServerGetKeyboard(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    server->keyboard =
        wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->keyboard, &s_serverKeyboard, server, nullptr);
    ServerSendKeymap(server);
    wl_keyboard_send_repeat_info(server->keyboard, server->repeatRate, server->repeatDelay);
}

static inline void ServerSetCursor(struct wl_client* client, struct wl_resource* resource,
                                   uint32_t serial, struct wl_resource* surface, int32_t x,
                                   int32_t y)
{
    (void)client;
    (void)serial;
    Server* server = wl_resource_get_user_data(resource);
    server->cursor.hides += surface == nullptr;
    server->cursor.surfaceShown = surface != nullptr;
    SurfaceState* state = surface != nullptr ? ServerSurfaceState(server, surface) : nullptr;
    if (state != nullptr)
    {
        Cursor* cursor = &server->cursor;
        cursor->images += 1;
        cursor->hotspotX = x;
        cursor->hotspotY = y;
        cursor->width = state->width;
        cursor->height = state->height;
        cursor->scale = state->scale;
        cursor->pixel = state->pixel;
        cursor->destinationWidth = state->destinationWidth;
        cursor->destinationHeight = state->destinationHeight;
    }
}

static const struct wl_pointer_interface s_serverPointer = {
    .set_cursor = ServerSetCursor,
    .release = ServerDestroyResource,
};

static inline void ServerGetPointer(struct wl_client* client, struct wl_resource* resource,
                                    uint32_t id)
{
    Server* server = wl_resource_get_user_data(resource);
    server->pointer =
        wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->pointer, &s_serverPointer, server, nullptr);
}

static const struct wl_touch_interface s_serverTouch = {
    .release = ServerDestroyResource,
};

static inline void ServerGetTouch(struct wl_client* client, struct wl_resource* resource,
                                  uint32_t id)
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

static inline void ServerBindSeat(struct wl_client* client, void* data, uint32_t version,
                                  uint32_t id)
{
    struct wl_resource* resource = wl_resource_create(client, &wl_seat_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverSeat, data, nullptr);
    wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER |
                                            WL_SEAT_CAPABILITY_TOUCH);
}

static inline void ServerSetShape(struct wl_client* client, struct wl_resource* resource,
                                  uint32_t serial, uint32_t shape)
{
    (void)client;
    (void)serial;
    Server* server = wl_resource_get_user_data(resource);
    server->cursor.shape = shape;
    server->cursor.surfaceShown = false;
}

static const struct wp_cursor_shape_device_v1_interface s_serverShapeDevice = {
    .destroy = ServerDestroyResource,
    .set_shape = ServerSetShape,
};

static inline void ServerGetShapeDevice(struct wl_client* client, struct wl_resource* resource,
                                        uint32_t id, struct wl_resource* pointer)
{
    (void)pointer;
    struct wl_resource* device = wl_resource_create(client, &wp_cursor_shape_device_v1_interface,
                                                    wl_resource_get_version(resource), id);
    wl_resource_set_implementation(device, &s_serverShapeDevice,
                                   wl_resource_get_user_data(resource), nullptr);
}

static const struct wp_cursor_shape_manager_v1_interface s_serverShapes = {
    .destroy = ServerDestroyResource,
    .get_pointer = ServerGetShapeDevice,
};

static inline void ServerBindShapes(struct wl_client* client, void* data, uint32_t version,
                                    uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &wp_cursor_shape_manager_v1_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverShapes, data, nullptr);
}

// A constraint ends when the client destroys it.
static inline void ServerEndLock(struct wl_resource* resource)
{
    Server* server = wl_resource_get_user_data(resource);
    server->cursor.locked = false;
}

static inline void ServerEndConfine(struct wl_resource* resource)
{
    Server* server = wl_resource_get_user_data(resource);
    server->cursor.confined = false;
}

static const struct zwp_locked_pointer_v1_interface s_serverLocked = {
    .destroy = ServerDestroyResource,
};

static const struct zwp_confined_pointer_v1_interface s_serverConfined = {
    .destroy = ServerDestroyResource,
};

static inline void ServerLock(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                              struct wl_resource* surface, struct wl_resource* pointer,
                              struct wl_resource* region, uint32_t lifetime)
{
    (void)surface;
    (void)pointer;
    (void)region;
    (void)lifetime;
    Server* server = wl_resource_get_user_data(resource);
    struct wl_resource* locked = wl_resource_create(client, &zwp_locked_pointer_v1_interface,
                                                    wl_resource_get_version(resource), id);
    wl_resource_set_implementation(locked, &s_serverLocked, server, ServerEndLock);
    server->cursor.locked = true;
    zwp_locked_pointer_v1_send_locked(locked);
}

static inline void ServerConfine(struct wl_client* client, struct wl_resource* resource,
                                 uint32_t id, struct wl_resource* surface,
                                 struct wl_resource* pointer, struct wl_resource* region,
                                 uint32_t lifetime)
{
    (void)surface;
    (void)pointer;
    (void)region;
    (void)lifetime;
    Server* server = wl_resource_get_user_data(resource);
    struct wl_resource* confined = wl_resource_create(client, &zwp_confined_pointer_v1_interface,
                                                      wl_resource_get_version(resource), id);
    wl_resource_set_implementation(confined, &s_serverConfined, server, ServerEndConfine);
    server->cursor.confined = true;
    zwp_confined_pointer_v1_send_confined(confined);
}

static const struct zwp_pointer_constraints_v1_interface s_serverConstraints = {
    .destroy = ServerDestroyResource,
    .lock_pointer = ServerLock,
    .confine_pointer = ServerConfine,
};

static inline void ServerBindConstraints(struct wl_client* client, void* data, uint32_t version,
                                         uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &zwp_pointer_constraints_v1_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverConstraints, data, nullptr);
}

static const struct zwp_relative_pointer_v1_interface s_serverRelative = {
    .destroy = ServerDestroyResource,
};

static inline void ServerGetRelative(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t id, struct wl_resource* pointer)
{
    (void)pointer;
    Server* server = wl_resource_get_user_data(resource);
    server->relative = wl_resource_create(client, &zwp_relative_pointer_v1_interface,
                                          wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->relative, &s_serverRelative, server, nullptr);
}

static const struct zwp_relative_pointer_manager_v1_interface s_serverRelatives = {
    .destroy = ServerDestroyResource,
    .get_relative_pointer = ServerGetRelative,
};

static inline void ServerBindRelatives(struct wl_client* client, void* data, uint32_t version,
                                       uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &zwp_relative_pointer_manager_v1_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverRelatives, data, nullptr);
}

static inline void ServerEnableText(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    server->pendingText.enabled = true;
}

static inline void ServerDisableText(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    server->pendingText.enabled = false;
}

static inline void ServerContentType(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t hint, uint32_t purpose)
{
    (void)client;
    (void)resource;
    (void)hint;
    (void)purpose;
}

static inline void ServerCaret(struct wl_client* client, struct wl_resource* resource, int32_t x,
                               int32_t y, int32_t width, int32_t height)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    server->pendingText.x = x;
    server->pendingText.y = y;
    server->pendingText.width = width;
    server->pendingText.height = height;
}

static inline void ServerCommitText(struct wl_client* client, struct wl_resource* resource)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    server->pendingText.commits = server->text.commits + 1;
    server->text = server->pendingText;
}

static const struct zwp_text_input_v3_interface s_serverTextInput = {
    .destroy = ServerDestroyResource,
    .enable = ServerEnableText,
    .disable = ServerDisableText,
    .set_content_type = ServerContentType,
    .set_cursor_rectangle = ServerCaret,
    .commit = ServerCommitText,
};

static inline void ServerGetTextInput(struct wl_client* client, struct wl_resource* resource,
                                      uint32_t id, struct wl_resource* seat)
{
    (void)seat;
    Server* server = wl_resource_get_user_data(resource);
    server->textInput = wl_resource_create(client, &zwp_text_input_v3_interface,
                                           wl_resource_get_version(resource), id);
    wl_resource_set_implementation(server->textInput, &s_serverTextInput, server, nullptr);
}

static const struct zwp_text_input_manager_v3_interface s_serverTextInputs = {
    .destroy = ServerDestroyResource,
    .get_text_input = ServerGetTextInput,
};

static inline void ServerBindTextInputs(struct wl_client* client, void* data, uint32_t version,
                                        uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &zwp_text_input_manager_v3_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverTextInputs, data, nullptr);
}

static inline void ServerPosition(struct wl_client* client, struct wl_resource* resource, int32_t x,
                                  int32_t y)
{
    (void)client;
    Server* server = wl_resource_get_user_data(resource);
    for (int i = 0; i < server->shell.parts; i++)
    {
        if (server->parts[i].subsurface == resource)
        {
            server->parts[i].x = x;
            server->parts[i].y = y;
        }
    }
}

static const struct wl_subsurface_interface s_serverSubsurface = {
    .destroy = ServerDestroyResource,
    .set_position = ServerPosition,
    .set_sync = ServerNoRequest,
    .set_desync = ServerNoRequest,
};

static inline void ServerGetSubsurface(struct wl_client* client, struct wl_resource* resource,
                                       uint32_t id, struct wl_resource* surface,
                                       struct wl_resource* parent)
{
    (void)parent;
    Server* server = wl_resource_get_user_data(resource);
    struct wl_resource* subsurface =
        wl_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(subsurface, &s_serverSubsurface, server, nullptr);
    if (server->shell.parts < SERVER_PARTS)
    {
        int index = server->shell.parts++;
        server->parts[index].surface = surface;
        server->parts[index].subsurface = subsurface;
    }
}

static const struct wl_subcompositor_interface s_serverSubcompositor = {
    .destroy = ServerDestroyResource,
    .get_subsurface = ServerGetSubsurface,
};

static inline void ServerBindSubcompositor(struct wl_client* client, void* data, uint32_t version,
                                           uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &wl_subcompositor_interface, version, id);
    wl_resource_set_implementation(resource, &s_serverSubcompositor, data, nullptr);
}

static inline void* ServerRun(void* data)
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
static inline bool ServerCompileKeymap(Server* server, const char* layout, const char* variant)
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

static inline void ServerSetSource(struct wl_client* client, struct wl_resource* resource,
                                   wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static inline void ServerSetDestination(struct wl_client* client, struct wl_resource* resource,
                                        int32_t width, int32_t height)
{
    (void)client;
    SurfaceState* state = wl_resource_get_user_data(resource);
    if (state != nullptr)
    {
        state->destinationWidth = width;
        state->destinationHeight = height;
    }
}

static const struct wp_viewport_interface s_serverViewport = {
    .destroy = ServerDestroyResource,
    .set_source = ServerSetSource,
    .set_destination = ServerSetDestination,
};

static inline void ServerGetViewport(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t id, struct wl_resource* surface)
{
    Server* server = wl_resource_get_user_data(resource);
    struct wl_resource* viewport =
        wl_resource_create(client, &wp_viewport_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(viewport, &s_serverViewport, ServerSurfaceState(server, surface),
                                   nullptr);
}

static const struct wp_viewporter_interface s_serverViewporter = {
    .destroy = ServerDestroyResource,
    .get_viewport = ServerGetViewport,
};

static inline void ServerBindViewporter(struct wl_client* client, void* data, uint32_t version,
                                        uint32_t id)
{
    struct wl_resource* resource =
        wl_resource_create(client, &wp_viewporter_interface, (int)version, id);
    wl_resource_set_implementation(resource, &s_serverViewporter, data, nullptr);
}

// Offers the viewporter, for a test that wants it, before the client
// connects.
static inline void ServerAddViewporter(Server* server)
{
    pthread_mutex_lock(&server->lock);
    wl_global_create(server->display, &wp_viewporter_interface, 1, server, ServerBindViewporter);
    pthread_mutex_unlock(&server->lock);
}

// Starts the compositor on a new socket, which WAYLAND_DISPLAY then
// names. False when it cannot.
static inline bool ServerStart(Server* server, const char* layout, const char* variant)
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
    server->shellGlobal =
        wl_global_create(server->display, &xdg_wm_base_interface, 5, server, ServerBindWmBase);
    wl_global_create(server->display, &wl_seat_interface, 8, server, ServerBindSeat);
    wl_global_create(server->display, &wp_cursor_shape_manager_v1_interface, 1, server,
                     ServerBindShapes);
    wl_global_create(server->display, &zwp_pointer_constraints_v1_interface, 1, server,
                     ServerBindConstraints);
    wl_global_create(server->display, &zwp_relative_pointer_manager_v1_interface, 1, server,
                     ServerBindRelatives);
    wl_global_create(server->display, &zwp_text_input_manager_v3_interface, 1, server,
                     ServerBindTextInputs);
    wl_global_create(server->display, &wl_subcompositor_interface, 1, server,
                     ServerBindSubcompositor);
    wl_display_init_shm(server->display);
    setenv("WAYLAND_DISPLAY", server->socket, 1);
    pthread_mutex_init(&server->lock, nullptr);
    return pthread_create(&server->thread, nullptr, ServerRun, server) == 0;
}

static inline void ServerStop(Server* server)
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
static inline void ServerEnter(Server* server)
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

static inline void ServerLeave(Server* server)
{
    pthread_mutex_lock(&server->lock);
    wl_keyboard_send_leave(server->keyboard, ++server->serial, server->surface);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// A key by its evdev code, and the modifier state and layout group.
static inline void ServerKey(Server* server, uint32_t evdev, bool pressed)
{
    pthread_mutex_lock(&server->lock);
    wl_keyboard_send_key(server->keyboard, ++server->serial, 1000, evdev,
                         pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static inline void ServerModifiers(Server* server, uint32_t depressed, uint32_t group)
{
    pthread_mutex_lock(&server->lock);
    wl_keyboard_send_modifiers(server->keyboard, ++server->serial, depressed, 0, 0, group);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// Pointer events, each in a frame of its own unless noted. The pointer
// enters a surface, the toplevel's content where none is named.
static inline void ServerPointerEnterOn(Server* server, struct wl_resource* surface, double x,
                                        double y)
{
    pthread_mutex_lock(&server->lock);
    wl_pointer_send_enter(server->pointer, ++server->serial,
                          surface != nullptr ? surface : server->surface, wl_fixed_from_double(x),
                          wl_fixed_from_double(y));
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static inline void ServerPointerEnter(Server* server, double x, double y)
{
    ServerPointerEnterOn(server, nullptr, x, y);
}

static inline void ServerPointerLeaveFrom(Server* server, struct wl_resource* surface)
{
    pthread_mutex_lock(&server->lock);
    wl_pointer_send_leave(server->pointer, ++server->serial,
                          surface != nullptr ? surface : server->surface);
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// Two motions in one frame, of which the program sees the last.
static inline void ServerPointerMotion(Server* server, double x, double y)
{
    pthread_mutex_lock(&server->lock);
    wl_pointer_send_motion(server->pointer, 1000, wl_fixed_from_double(x - 5.0),
                           wl_fixed_from_double(y - 5.0));
    wl_pointer_send_motion(server->pointer, 1000, wl_fixed_from_double(x), wl_fixed_from_double(y));
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static inline void ServerButton(Server* server, uint32_t button, bool pressed)
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
static inline void ServerWheel(Server* server, int32_t steps120)
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
static inline void ServerTouchStroke(Server* server, int32_t id)
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

// Relative motion: accelerated by half again, and the raw distance.
static inline void ServerRelativeMotion(Server* server, double dx, double dy)
{
    pthread_mutex_lock(&server->lock);
    zwp_relative_pointer_v1_send_relative_motion(
        server->relative, 0, 0, wl_fixed_from_double(dx * 1.5), wl_fixed_from_double(dy * 1.5),
        wl_fixed_from_double(dx), wl_fixed_from_double(dy));
    wl_pointer_send_frame(server->pointer);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// What the client has asked for of the cursor so far.
static inline Cursor ServerCursor(Server* server)
{
    pthread_mutex_lock(&server->lock);
    Cursor cursor = server->cursor;
    pthread_mutex_unlock(&server->lock);
    return cursor;
}

// The seat's text input focuses on the last surface made.
static inline void ServerTextEnter(Server* server)
{
    pthread_mutex_lock(&server->lock);
    zwp_text_input_v3_send_enter(server->textInput, server->surface);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

// The input method's next state: a composition with its cursor (NULL
// for none) and committed text (NULL for none), applied by done.
static inline void ServerCompose(Server* server, const char* preedit, int32_t begin, int32_t end,
                                 const char* commit)
{
    pthread_mutex_lock(&server->lock);
    if (commit != nullptr)
    {
        zwp_text_input_v3_send_commit_string(server->textInput, commit);
    }
    if (preedit != nullptr)
    {
        zwp_text_input_v3_send_preedit_string(server->textInput, preedit, begin, end);
    }
    zwp_text_input_v3_send_done(server->textInput, server->text.commits);
    wl_display_flush_clients(server->display);
    pthread_mutex_unlock(&server->lock);
}

static inline TextState ServerText(Server* server)
{
    pthread_mutex_lock(&server->lock);
    TextState text = server->text;
    pthread_mutex_unlock(&server->lock);
    return text;
}

static inline void ServerPointerLeave(Server* server)
{
    ServerPointerLeaveFrom(server, nullptr);
}

static inline Shell ServerShell(Server* server)
{
    pthread_mutex_lock(&server->lock);
    Shell shell = server->shell;
    pthread_mutex_unlock(&server->lock);
    return shell;
}

// The subsurface placed at a point relative to its parent, or NULL.
static inline struct wl_resource* ServerPartAt(Server* server, int32_t x, int32_t y)
{
    pthread_mutex_lock(&server->lock);
    struct wl_resource* found = nullptr;
    for (int i = 0; i < server->shell.parts; i++)
    {
        found =
            server->parts[i].x == x && server->parts[i].y == y ? server->parts[i].surface : found;
    }
    pthread_mutex_unlock(&server->lock);
    return found;
}

#endif // MAUL_WINDOW_TEST_WAYLAND_SERVER_H
