// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the Wayland backend keeps: the connection, the globals it bound,
// and per window slot and per output the objects and the state the
// compositor has told. One block from the context's allocator holds it
// all.

#ifndef MAUL_WINDOW_SRC_WAYLAND_H
#define MAUL_WINDOW_SRC_WAYLAND_H

#include "core.h"
#include "wayland_api.h"

#include <time.h>

typedef struct mwinWaylandPlatform mwinWaylandPlatform;

// A window's objects, and what the compositor proposed in the configure
// sequence that is still open.
typedef struct mwinWaylandWindow
{
    mwinWaylandPlatform* platform;
    uint32_t slot;
    struct wl_surface* surface;
    struct xdg_surface* xdgSurface;
    struct xdg_toplevel* toplevel;
    struct zxdg_toplevel_decoration_v1* decoration;
    struct wp_fractional_scale_v1* fractionalScale;
    struct wp_viewport* viewport;
    // The proposal of the open configure sequence: a size of 0 leaves the
    // size to the window.
    int32_t proposedWidth;
    int32_t proposedHeight;
    mwinWindowMode proposedMode;
    bool proposedActivated;
    bool proposedSuspended;
    // The size the window has, in logical units, and its scale: in 120ths
    // from fractional scaling, or an integer buffer scale.
    mwinSize size;
    uint32_t scale120;
    int32_t bufferScale;
    // The first configure made the window (mwin_eventWindowCreated).
    bool configured;
    // Minimized on request. No configure says so, nor that the window
    // came back; its next activation does.
    bool minimized;
    // A mode request waits for the compositor's next configure, or -1.
    int32_t modeRequest;
} mwinWaylandWindow;

// An output the compositor announced, and the monitor slot it fills, or
// -1 before its first done event.
typedef struct mwinWaylandOutput
{
    mwinWaylandPlatform* platform;
    struct wl_output* output;
    uint32_t name;
    int32_t monitor;
    // Facts arrive one event at a time; done makes them true together.
    mwinMonitorInfo info;
    int32_t x;
    int32_t y;
    int32_t transform;
    int32_t scale;
} mwinWaylandOutput;

struct mwinWaylandPlatform
{
    mwinWaylandApi api;
    mwinContext* context;
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_compositor* compositor;
    struct xdg_wm_base* wmBase;
    struct zxdg_decoration_manager_v1* decorations;
    struct wp_fractional_scale_manager_v1* fractionalScale;
    struct wp_viewporter* viewporter;
    // The connection failed; the loop stops.
    bool failed;
    // One per window slot, and one per monitor slot.
    mwinWaylandWindow* windows;
    mwinWaylandOutput* outputs;
    // A NUL-terminated copy of a title, titleBytes + 1 bytes.
    char* title;
};

// Nanoseconds on the monotonic clock, which Wayland's input timestamps
// also count.
static inline uint64_t mwinWaylandNow(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

#endif // MAUL_WINDOW_SRC_WAYLAND_H
