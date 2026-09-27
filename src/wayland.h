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
    // The cursor the program asked for over the window, and the pointer
    // constraint its mode needs.
    mwinCursorMode cursorMode;
    mwinCursorShape cursorShape;
    struct zwp_locked_pointer_v1* locked;
    struct zwp_confined_pointer_v1* confined;
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

// The seat's pointer: the window it is over, what a pointer frame has
// gathered so far, and the last press for counting quick clicks.
typedef struct mwinWaylandPointer
{
    struct wl_pointer* pointer;
    // The window slot under the pointer, or -1, and the serial of the
    // enter event, which cursor requests quote.
    int32_t focus;
    uint32_t enterSerial;
    mwinPosition position;
    uint8_t buttons;
    // Gathered until the frame event: motion, and per axis (0 vertical,
    // 1 horizontal) high-resolution steps (120ths of a detent), discrete
    // steps and continuous distance, of which the first present counts.
    bool moved;
    uint64_t timeNs;
    int32_t steps120[2];
    int32_t steps[2];
    double distance[2];
    uint8_t axisKinds[2];
    // The last press: its button, time and place, and the clicks it made.
    mwinMouseButton clickButton;
    uint64_t clickNs;
    mwinPosition clickPosition;
    uint8_t clicks;
    // The pointer's cursor shape device and relative motion, where the
    // compositor has them.
    struct wp_cursor_shape_device_v1* shapeDevice;
    struct zwp_relative_pointer_v1* relative;
} mwinWaylandPointer;

// Cursor images from the cursor theme, for a compositor without cursor
// shapes: the theme at the scale it was loaded for, and the surface the
// image is shown on.
typedef struct mwinWaylandCursorTheme
{
    struct wl_cursor_theme* theme;
    int32_t scale;
    struct wl_surface* surface;
} mwinWaylandCursorTheme;

// The most touches followed at once.
#define MWIN_WAYLAND_TOUCHES 16

// The seat's touch screen: each touch point by its id, with the window
// it began on.
typedef struct mwinWaylandTouch
{
    struct wl_touch* touch;
    struct
    {
        int32_t id;
        int32_t slot;
        mwinPosition position;
        bool active;
    } points[MWIN_WAYLAND_TOUCHES];
} mwinWaylandTouch;

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
    struct wp_cursor_shape_manager_v1* cursorShapes;
    struct zwp_pointer_constraints_v1* constraints;
    struct zwp_relative_pointer_manager_v1* relativePointers;
    struct wl_shm* shm;
    mwinWaylandCursorTheme cursorTheme;
    // The first seat, its registry name, and its keyboard. libxkbcommon
    // loads with the context; without it there is no keyboard.
    struct wl_seat* seat;
    uint32_t seatName;
    mwinXkbApi xkb;
    mwinWaylandKeyboard keyboard;
    mwinWaylandPointer pointer;
    mwinWaylandTouch touch;
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
