// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the Win32 backend keeps: the window class, per window slot the
// HWND and what the program asked of it, and per monitor slot the
// HMONITOR it stands for. One block from the context's allocator holds
// it all.

#ifndef MAUL_WINDOW_SRC_WIN32_H
#define MAUL_WINDOW_SRC_WIN32_H

#include "clicks.h"
#include "core.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string.h>
#include <windows.h>

typedef struct mwinWin32Platform mwinWin32Platform;

// The touches followed at once on a window; more are left out.
#define MWIN_WIN32_TOUCHES 10

// A window, and what the program asked of it that Win32 keeps nowhere.
typedef struct mwinWin32Window
{
    mwinWin32Platform* platform;
    uint32_t slot;
    HWND hwnd;
    // The client area in pixels, and its place on the desktop.
    uint32_t width;
    uint32_t height;
    int32_t x;
    int32_t y;
    // Dots per inch of the monitor the window is on.
    uint32_t dpi;
    // The monitor slot it is on, or -1.
    int32_t monitor;
    // The size bounds of the client area in logical units, 0 for none,
    // and the aspect ratio, 0 for none.
    mwinSize minimum;
    mwinSize maximum;
    uint32_t aspectWidth;
    uint32_t aspectHeight;
    // Borderless full screen, and the placement to come back to.
    bool fullscreen;
    WINDOWPLACEMENT restore;
    // The cursor the program asked for over the window.
    mwinCursorMode cursorMode;
    mwinCursorShape cursorShape;
    // The pointer is over the window (a leave is asked for), the
    // buttons held, and the last press.
    bool tracking;
    uint8_t buttons;
    mwinClickCounter clicks;
    // The first half of a character outside the BMP, 0 for none.
    WCHAR highSurrogate;
    // The pen's flags at its last record.
    mwinPenFlags penFlags;
    // Where the program's caret is, for the input method's windows.
    mwinRect caret;
    // The touches down on the window, by pointer id; 0 for none.
    UINT32 touches[MWIN_WIN32_TOUCHES];
} mwinWin32Window;

// A monitor by its HMONITOR, which Windows keeps while it is connected.
typedef struct mwinWin32Output
{
    HMONITOR handle;
    int32_t monitor;
    bool seen;
} mwinWin32Output;

struct mwinWin32Platform
{
    mwinContext* context;
    HINSTANCE instance;
    ATOM windowClass;
    // A UTF-16 copy of a title with its terminator: titleBytes + 1 units.
    WCHAR* title;
    // An input method's string as read, its attributes and as UTF-8:
    // textBytesPerWindow of each.
    WCHAR* imeUnits;
    BYTE* imeAttributes;
    char* imeBytes;
    // The preferred languages as read, localeBytes + 2 units, and as
    // UTF-8, localeBytes.
    WCHAR* localeUnits;
    char* localeText;
    // One per window slot, and one per monitor slot.
    mwinWin32Window* windows;
    mwinWin32Output* outputs;
    // A window is being moved or sized: Windows runs its own loop, and
    // frames run from its timer.
    bool inSizeMove;
    // The mouse's raw input goes to the window with focus.
    bool rawInput;
};

// The pointer a message parameter or window long carries. Win32 passes
// pointers through its integer types; the bytes are copied, not cast.
static inline void* mwinWin32Pointer(LONG_PTR value)
{
    void* pointer = nullptr;
    memcpy((void*)&pointer, (const void*)&value, sizeof(pointer));
    return pointer;
}

// The logical units per pixel of a DPI.
static inline float mwinWin32Scale(uint32_t dpi)
{
    return (float)dpi / (float)USER_DEFAULT_SCREEN_DPI;
}

// Nanoseconds on the performance counter.
static inline uint64_t mwinWin32Now(void)
{
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    uint64_t seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    uint64_t rest = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    return seconds * 1000000000u + rest * 1000000000u / (uint64_t)frequency.QuadPart;
}

#endif // MAUL_WINDOW_SRC_WIN32_H
