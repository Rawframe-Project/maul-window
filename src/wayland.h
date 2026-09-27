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
#include "xkb_api.h"

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

// The seat's keyboard: the keymap and state from the compositor, compose
// sequences from the locale's table, the window with keyboard focus, and
// key repeat, which a Wayland client does itself.
typedef struct mwinWaylandKeyboard
{
    struct wl_keyboard* keyboard;
    struct xkb_context* context;
    struct xkb_keymap* keymap;
    struct xkb_state* state;
    struct xkb_compose_table* composeTable;
    struct xkb_compose_state* compose;
    // The window slot with keyboard focus, or -1.
    int32_t focus;
    mwinModifiers modifiers;
    uint32_t layout;
    // Keys held since focus came, for the reset when it goes.
    uint32_t held;
    // Repeats per second (0 for none) and the delay before the first.
    int32_t repeatRate;
    int32_t repeatDelayMs;
    // The evdev code of the key that repeats, 0 for none, and when.
    uint32_t repeatKey;
    uint64_t repeatNextNs;
} mwinWaylandKeyboard;

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
    // The first seat, its registry name, and its keyboard. libxkbcommon
    // loads with the context; without it there is no keyboard.
    struct wl_seat* seat;
    uint32_t seatName;
    mwinXkbApi xkb;
    mwinWaylandKeyboard keyboard;
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

// A Wayland event time, milliseconds on the monotonic clock in 32 bits,
// in nanoseconds: the time of the latest wrap of the millisecond count
// before now. A time that seems far off is taken as now.
static inline uint64_t mwinWaylandTime(uint32_t milliseconds)
{
    uint64_t now = mwinWaylandNow();
    uint32_t age = (uint32_t)(now / 1000000u) - milliseconds;
    return age < 60000u && (uint64_t)age * 1000000u <= now ? now - (uint64_t)age * 1000000u : now;
}

// The slot of the window whose surface this is, or -1.
static inline int32_t mwinWaylandSlotOf(const mwinWaylandPlatform* platform,
                                        const struct wl_surface* surface)
{
    for (uint32_t i = 0; surface != nullptr && i < platform->context->limits.windows; i++)
    {
        if (platform->windows[i].surface == surface)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

#endif // MAUL_WINDOW_SRC_WAYLAND_H
