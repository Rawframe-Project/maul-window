// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the macOS backend keeps (mwin-0024): per window slot its NSWindow,
// the library's content view with its CAMetalLayer, the window's delegate
// and what the program was told of it; per monitor slot the display
// shown there; what drives the program's frames; the keyboard's layout,
// the cursors, the pen and the gamepads. Objects are held with manual retain and release. Included
// by the backend's Objective-C files only.

#ifndef MAUL_WINDOW_SRC_MACOS_H
#define MAUL_WINDOW_SRC_MACOS_H

#include "core.h"
#include "pad_tracker.h"

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
    // The cursor asked for.
    mwinCursorMode cursorMode;
    mwinCursorShape cursorShape;
    // Whether the window accepts text, where its caret is, and an input
    // method's marked text (nil when none) with its selection in UTF-16
    // units.
    bool textInput;
    mwinRect caret;
    NSString* marked;
    NSRange markedSelection;
    // The pen's state over the window, as last posted.
    mwinPenFlags penFlags;
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
    // The blank cursor of the hiding modes, made when first needed, and
    // the slot of the window whose cursor is captured, plus one; 0 when
    // none is.
    NSCursor* blankCursor;
    uint32_t captured;
    // Whether the pen near the tablet shows its eraser end.
    bool penEraser;
    // GameController's pads, what tells of their connections, whether
    // one came or went since they were looked for, and whether they are
    // watched at all (from macOS 11.3).
    mwinPadTracker pads;
    id padObservers[2];
    bool padsChanged;
    bool padsStarted;
    // Each rumbled pad's motors, by its controller; nil before the first.
    id rumbles;
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

// An input method's marked text and its selection, committed text, and
// the marked text accepted as it is (macos_text.m); where the caret is
// on the screen; a text input request's outcome.
void mwinMacSetMarkedText(mwinMacPlatform* platform, uint32_t slot, id string, NSRange selected);
void mwinMacInsertText(mwinMacPlatform* platform, uint32_t slot, id string);
void mwinMacUnmarkText(mwinMacPlatform* platform, uint32_t slot);
NSRect mwinMacCaretOnScreen(const mwinMacPlatform* platform, uint32_t slot);

// Lets every input source compose into a view, or only Roman keyboard
// layouts.
void mwinMacLimitInputSources(NSView* view, bool all);
mwinOutcome mwinMacSetTextInput(mwinMacPlatform* platform, uint32_t slot, bool enabled,
                                mwinRect caret);

// The cursor a window shows over its view (macos_cursor.m); a cursor
// mode or shape request's outcome; capturing the cursor while the window
// is the key window, or letting it go; letting every cursor go at the
// stop.
NSCursor* mwinMacCursorOf(mwinMacPlatform* platform, const mwinMacWindow* window);
mwinOutcome mwinMacSetCursorMode(mwinMacPlatform* platform, uint32_t slot, mwinCursorMode mode);
mwinOutcome mwinMacSetCursorShape(mwinMacPlatform* platform, uint32_t slot, mwinCursorShape shape);
void mwinMacApplyCapture(mwinMacPlatform* platform, uint32_t slot, bool focused);
void mwinMacForgetCursors(mwinMacPlatform* platform);

// Posts a pen's event as pen records: true when the event was a pen's
// (macos_pen.m). Notes which end of the pen came near the tablet.
bool mwinMacTakePen(mwinMacPlatform* platform, uint32_t slot, NSEvent* event);
void mwinMacPenProximity(mwinMacPlatform* platform, NSEvent* event);

// Watches GameController's pads from the start of the backend to its
// stop, and reads them at each pump (macos_pad.m, with the gamepad
// component).
void mwinMacStartPads(mwinMacPlatform* platform);
void mwinMacStopPads(mwinMacPlatform* platform);
void mwinMacPumpPads(mwinMacPlatform* platform, uint64_t nowNs);

// Whether a controller has motors; runs them, each from 0 to 1, both 0
// stopping them; lets go of the motors of controllers not kept, or of
// all with nil (macos_rumble.m).
bool mwinMacCanRumble(id controller);
bool mwinMacRumble(mwinMacPlatform* platform, id controller, float low, float high);
void mwinMacForgetRumbles(mwinMacPlatform* platform, NSArray* kept);

// The backend's window operations (backend.h).
void mwinMacCreateWindow(mwinContext* context, uint32_t slot);
void mwinMacDestroyWindow(mwinContext* context, uint32_t slot);
void mwinMacSubmit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_MACOS_H
