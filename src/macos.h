// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the macOS backend keeps (mwin-0024): per window slot its NSWindow,
// the library's content view with its CAMetalLayer, the window's delegate
// and what the program was told of it; per monitor slot the display
// shown there; what drives the program's frames; and the keyboard's
// layout. Objects are held with manual retain and release. Included by
// the backend's Objective-C files only.

#ifndef MAUL_WINDOW_SRC_MACOS_H
#define MAUL_WINDOW_SRC_MACOS_H

#include "core.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>

typedef struct mwinMacPlatform mwinMacPlatform;

// A window: nil objects in a free slot. Its size in pixels, its scale,
// its place in logical units from the top left of the primary screen,
// its monitor slot (-1 before the first) and its mode, as last posted.
typedef struct mwinMacWindow
{
    NSWindow* window;
    NSView* view;
    CAMetalLayer* layer;
    id delegate;
    uint32_t width;
    uint32_t height;
    float scale;
    mwinPosition position;
    int32_t monitor;
    mwinWindowMode mode;
    // The mouse buttons held over the view (bit b - 1 for button b), and
    // whether the pointer is over it.
    uint8_t buttons;
    bool pointerInside;
} mwinMacWindow;

struct mwinMacPlatform
{
    mwinContext* context;
    mwinMacWindow* windows;
    // Per monitor slot, the CGDirectDisplayID of its screen; 0 when free.
    uint32_t* displays;
    // What calls the program's frames: a display link, or a timer before
    // macOS 14, and the object they call.
    id driver;
    id stepper;
    id screensObserver;
    // The current keyboard input source (a TISInputSourceRef, held), what
    // tells of its changes, and what hands Command's key releases on.
    const void* layout;
    id layoutObserver;
    id keyUpMonitor;
};

// The platform of a context whose backend is macOS.
mwinMacPlatform* mwinMacPlatformOf(const mwinContext* context);

// Nanoseconds on the clock NSEvent timestamps count (the uptime without
// sleep).
uint64_t mwinMacNow(void);

// Reads the screens into the monitor slots, adding, changing and
// removing monitors, and forgets them all at the end.
void mwinMacReadScreens(mwinMacPlatform* platform, uint64_t timeNs);
void mwinMacForgetScreens(mwinMacPlatform* platform);

// The monitor slot of a screen, or -1.
int32_t mwinMacMonitorOf(const mwinMacPlatform* platform, NSScreen* screen);

// The height of the primary screen in points, which turns AppKit's
// bottom-left coordinates into the contract's top-left ones.
CGFloat mwinMacPrimaryHeight(void);

// A window's content view, made retained: the view class of the slot's
// window (macos_view.m).
NSView* mwinMacCreateView(mwinMacPlatform* platform, uint32_t slot, NSRect frame);

// The physical key of a virtual key code; what the current layout makes
// of a key with no modifier; the modifiers of an event's flags.
mwinKeyCode mwinMacCodeOf(uint16_t virtualKey);
mwinKey mwinMacMeaningOf(const mwinMacPlatform* platform, uint16_t virtualKey, mwinKeyCode code);
mwinModifiers mwinMacModifiersOf(NSEventModifierFlags flags);

// What a flagsChanged: event did to a key: 1 pressed, 0 released, 2
// toggled (Caps Lock), -1 when the key is no modifier.
int mwinMacModifierChange(mwinKeyCode code, NSEventModifierFlags flags);

// The backend's mapKeyCode and keyboardLayout.
mwinKey mwinMacMapKeyCode(const mwinMacPlatform* platform, mwinKeyCode code);
mwinResult mwinMacKeyboardLayout(const mwinMacPlatform* platform, char* buffer, size_t capacity,
                                 size_t* lengthOut);

// Reads the keyboard layout and watches its changes and Command's key
// releases, from the start of the backend to its stop.
void mwinMacWatchKeyboard(mwinMacPlatform* platform);
void mwinMacUnwatchKeyboard(mwinMacPlatform* platform);

// The backend's window operations (backend.h).
void mwinMacCreateWindow(mwinContext* context, uint32_t slot);
void mwinMacDestroyWindow(mwinContext* context, uint32_t slot);
void mwinMacSubmit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_MACOS_H
