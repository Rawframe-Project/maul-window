// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the web backend keeps. The page's side lives in JavaScript, one
// object per context: the canvases, their observers and listeners, and a
// queue of what the browser reported, drained at the start of each
// frame. This side keeps per window slot what the program was told.

#ifndef MAUL_WINDOW_SRC_WEB_H
#define MAUL_WINDOW_SRC_WEB_H

#include "core.h"

// A canvas's selector as the native handles give it: "#" and its id.
#define MWIN_WEB_SELECTOR_BYTES 128

// The most of navigator.languages read at once.
#define MWIN_WEB_LOCALE_BYTES 512

// A window: its canvas, and its size, pixels and scale as last posted.
typedef struct mwinWebWindow
{
    char selector[MWIN_WEB_SELECTOR_BYTES];
    uint32_t selectorLength;
    mwinSize size;
    mwinPixelSize pixels;
    float scale;
    bool fullscreen;
} mwinWebWindow;

typedef struct mwinWebPlatform
{
    mwinContext* context;
    // One per window slot.
    mwinWebWindow* windows;
    // The screen's monitor slot, or -1.
    int32_t monitor;
    // The page's devicePixelRatio.
    float scale;
} mwinWebPlatform;

// What the page reports, one record at a time.
typedef enum mwinWebRecordKind
{
    mwin_webNone = 0,
    // A canvas's box: x, y its CSS size, z, w its pixels.
    mwin_webResized = 1,
    // devicePixelRatio changed: x.
    mwin_webScaled = 2,
    // A canvas gained (code 1) or lost focus.
    mwin_webFocus = 3,
    // A canvas became (code 1) or stopped being the fullscreen element.
    mwin_webFullscreen = 4,
    // A canvas's fullscreen request failed.
    mwin_webFullscreenFailed = 5,
    // The page was shown (code 1) or hidden.
    mwin_webVisibility = 6,
    // A preference the facts come from changed.
    mwin_webFacts = 7,
    // The preferred languages changed.
    mwin_webLocales = 8,
} mwinWebRecordKind;

typedef struct mwinWebRecord
{
    int32_t kind;
    int32_t slot;
    int32_t code;
    int32_t reserved;
    float x;
    float y;
    float z;
    float w;
    // The event's time on performance.now(), in milliseconds.
    double timeMs;
} mwinWebRecord;

// Milliseconds on performance.now() in nanoseconds.
static inline uint64_t mwinWebNanoseconds(double milliseconds)
{
    return milliseconds > 0.0 ? (uint64_t)(milliseconds * 1000000.0) : 0;
}

#endif // MAUL_WINDOW_SRC_WEB_H
