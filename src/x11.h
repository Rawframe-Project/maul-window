// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the X11 backend keeps: the connection, the screen, the atoms it
// uses, and per window slot and per monitor what the X server has told.
// One block from the context's allocator holds it all.

#ifndef MAUL_WINDOW_SRC_X11_H
#define MAUL_WINDOW_SRC_X11_H

#include "clicks.h"
#include "core.h"
#include "monotonic.h"
#include "x11_api.h"
#include "xkb_keyboard.h"

// The atoms the backend interns at start, in the order of s_atomNames
// in backend_x11.c.
enum
{
    mwin_atomWmProtocols,
    mwin_atomWmDeleteWindow,
    mwin_atomWmState,
    mwin_atomWmChangeState,
    mwin_atomNetWmPing,
    mwin_atomNetWmName,
    mwin_atomNetWmPid,
    mwin_atomUtf8String,
    mwin_atomNetWmState,
    mwin_atomNetWmStateFullscreen,
    mwin_atomNetWmStateMaximizedVert,
    mwin_atomNetWmStateMaximizedHorz,
    mwin_atomNetWmStateAbove,
    mwin_atomNetWmStateHidden,
    mwin_atomNetWmWindowOpacity,
    mwin_atomNetActiveWindow,
    mwin_atomNetSupportingWmCheck,
    mwin_atomMotifWmHints,
    MWIN_X11_ATOMS,
};

typedef struct mwinX11Platform mwinX11Platform;

// A window, and what the X server told of it last.
typedef struct mwinX11Window
{
    mwinX11Platform* platform;
    uint32_t slot;
    xcb_window_t window;
    // Its place on the desktop and its size, in pixels.
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    // The size bounds and aspect ratio of the normal hints, in pixels.
    uint32_t minimumWidth;
    uint32_t minimumHeight;
    uint32_t maximumWidth;
    uint32_t maximumHeight;
    uint32_t aspectWidth;
    uint32_t aspectHeight;
    // The monitor slot it is on, or -1.
    int32_t monitor;
    // A mode request waits for the window manager's _NET_WM_STATE, or
    // -1, until its deadline.
    int32_t modeRequest;
    uint64_t modeDeadlineNs;
    // The cursor the program asked for over the window, and whether the
    // pointer is grabbed to keep it inside.
    mwinCursorMode cursorMode;
    mwinCursorShape cursorShape;
    bool confined;
} mwinX11Window;

// The core keyboard through XKB: the device, XKB's event code, and the
// keys held, which tell the X server's repeats from new presses.
typedef struct mwinX11Keyboard
{
    mwinXkbKeyboard xkb;
    int32_t device;
    uint8_t event;
    uint8_t held[32];
} mwinX11Keyboard;

// The core pointer: the window it is over, or -1, where, the buttons
// held and the last press.
typedef struct mwinX11Pointer
{
    int32_t focus;
    mwinPosition position;
    uint8_t buttons;
    mwinClickCounter clicks;
} mwinX11Pointer;

// The cursors the backend made: an empty one that hides the pointer,
// and each shape loaded from the cursor theme, 0 before it is.
typedef struct mwinX11Cursors
{
    xcb_cursor_context_t* context;
    xcb_cursor_t blank;
    xcb_cursor_t shapes[16];
} mwinX11Cursors;

// A monitor from RandR, by the atom of its name.
typedef struct mwinX11Output
{
    xcb_atom_t name;
    int32_t monitor;
    bool seen;
} mwinX11Output;

struct mwinX11Platform
{
    mwinX11Api api;
    mwinContext* context;
    xcb_connection_t* connection;
    xcb_screen_t* screen;
    xcb_atom_t atoms[MWIN_X11_ATOMS];
    // A window manager that follows EWMH runs.
    bool windowManager;
    // Logical units per pixel from Xft.dpi, 1 where it is not set.
    float scale;
    // RandR's first event code, 0 without RandR 1.5, and XInput's major
    // opcode, 0 without XInput 2 raw motion.
    uint8_t randrEvent;
    uint8_t xinputOpcode;
    // libxkbcommon, and the keyboard, where both load.
    mwinXkbApi xkbApi;
    mwinX11Keyboard keyboard;
    mwinX11Pointer pointer;
    mwinX11Cursors cursors;
    // The connection failed; the loop stops.
    bool failed;
    // One per window slot, and one per monitor slot.
    mwinX11Window* windows;
    mwinX11Output* outputs;
};

// The slot of the window with an X id, or -1.
static inline int32_t mwinX11SlotOf(const mwinX11Platform* platform, xcb_window_t window)
{
    for (uint32_t i = 0; window != 0 && i < platform->context->limits.windows; i++)
    {
        if (platform->windows[i].window == window)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

#endif // MAUL_WINDOW_SRC_X11_H
