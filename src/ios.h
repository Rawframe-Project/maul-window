// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the iOS backend keeps (mwin-0025): per window slot its scene, its
// UIWindow, the view controller and the library's view with its
// CAMetalLayer, and what the program was told of it; per monitor slot
// the screen shown there and what was last said of it; the scene waiting
// for a window; what drives the program's frames; and whether the
// application runs. Objects are held with manual retain and
// release. Included by the backend's Objective-C files only.

#ifndef MAUL_WINDOW_SRC_IOS_H
#define MAUL_WINDOW_SRC_IOS_H

#include "core.h"

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

typedef struct mwinIOSPlatform mwinIOSPlatform;

// A window: nil objects in a free slot. Its size in pixels, its scale,
// its place in logical units on its screen, its safe area, its monitor
// slot (-1 before the first) and its mode, as last posted.
typedef struct mwinIOSWindow
{
    UIWindowScene* scene;
    UIWindow* window;
    UIViewController* controller;
    UIView* view;
    CAMetalLayer* layer;
    uint32_t width;
    uint32_t height;
    float scale;
    mwinPosition position;
    mwinInsets safeArea;
    int32_t monitor;
    mwinWindowMode mode;
    // The pointer's buttons held over the view (bit b - 1 for button b).
    uint8_t buttons;
} mwinIOSWindow;

// Where a touch is in its life, as UIKit's touch methods tell.
typedef enum mwinIOSTouchPhase
{
    mwin_iosTouchBegan,
    mwin_iosTouchMoved,
    mwin_iosTouchEnded,
    mwin_iosTouchCancelled,
} mwinIOSTouchPhase;

struct mwinIOSPlatform
{
    mwinContext* context;
    mwinIOSWindow* windows;
    // Per monitor slot, its UIScreen (held), nil when free, and what the
    // program was last told of it.
    id* screens;
    mwinMonitorInfo* screenInfo;
    // A scene of the application's role that has no window yet (held),
    // which the next window takes; nil when none waits.
    UIWindowScene* waitingScene;
    // What calls the program's frames: the display link, and the object
    // it calls.
    CADisplayLink* link;
    id stepper;
    // Whether the program was told the application stopped running.
    bool suspended;
};

// The platform of a context whose backend is iOS.
mwinIOSPlatform* mwinIOSPlatformOf(const mwinContext* context);

// Nanoseconds on the clock UIKit's events count (the uptime without
// sleep).
uint64_t mwinIOSNow(void);

// Reads the connected scenes' screens into the monitor slots, adding,
// changing and removing monitors as they differ from what was said, and
// forgets them all at the end (ios_output.m).
void mwinIOSReadScreens(mwinIOSPlatform* platform, uint64_t timeNs);
void mwinIOSForgetScreens(mwinIOSPlatform* platform);

// The monitor slot of a screen, or -1.
int32_t mwinIOSMonitorOf(const mwinIOSPlatform* platform, UIScreen* screen);

// A window's view, made retained, whose layer is a CAMetalLayer, and the
// controller that shows it (ios_view.m).
UIView* mwinIOSCreateView(mwinIOSPlatform* platform, uint32_t slot, CGRect frame);
UIViewController* mwinIOSCreateController(UIView* view);

// Unties a destroyed window's view, which UIKit may still lay out.
void mwinIOSForgetView(UIView* view);

// The contract's modifiers of UIKit's flags (ios_pointer.m).
mwinModifiers mwinIOSModifiersOf(UIKeyModifierFlags flags);

// Touches in a phase: fingers, the Pencil and the pointer's clicks; the
// pointer hovering; the pointer's scrolling (ios_pointer.m).
void mwinIOSTouches(mwinIOSPlatform* platform, uint32_t slot, NSSet<UITouch*>* touches,
                    UIEvent* event, mwinIOSTouchPhase phase);
void mwinIOSHover(mwinIOSPlatform* platform, uint32_t slot, UIHoverGestureRecognizer* hover);
void mwinIOSScroll(mwinIOSPlatform* platform, uint32_t slot, UIPanGestureRecognizer* pan);

// Posts what changed of a window's scale, size, mode, place, safe area
// and monitor, after UIKit laid it out (ios_window.m).
void mwinIOSLayout(mwinIOSPlatform* platform, uint32_t slot);

// The slot of the window a scene shows, or -1.
int32_t mwinIOSSlotOfScene(const mwinIOSPlatform* platform, UIScene* scene);

// The backend's window operations (ios_window.m).
void mwinIOSCreateWindow(mwinContext* context, uint32_t slot);
void mwinIOSDestroyWindow(mwinContext* context, uint32_t slot);
void mwinIOSSubmit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_IOS_H
