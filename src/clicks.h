// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Quick clicks, counted by the backends whose platforms have no
// double-click setting (Wayland, X11): a press of the same button
// within 500 ms and 4 logical units of the last counts one more.

#ifndef MAUL_WINDOW_SRC_CLICKS_H
#define MAUL_WINDOW_SRC_CLICKS_H

#include "maul-window/input.h"
#include "maul-window/window.h"

#include <math.h>

#define MWIN_DOUBLE_CLICK_NS       500000000u
#define MWIN_DOUBLE_CLICK_DISTANCE 4.0f

// The last press: its button, time and place, and the clicks it made.
typedef struct mwinClickCounter
{
    mwinMouseButton button;
    uint64_t timeNs;
    mwinPosition position;
    uint8_t clicks;
} mwinClickCounter;

// Counts a press: the clicks it completes.
static inline uint8_t mwinCountClick(mwinClickCounter* counter, mwinMouseButton button,
                                     mwinPosition position, uint64_t timeNs)
{
    bool quick = button == counter->button && timeNs >= counter->timeNs &&
                 timeNs - counter->timeNs <= MWIN_DOUBLE_CLICK_NS &&
                 fabsf(position.x - counter->position.x) <= MWIN_DOUBLE_CLICK_DISTANCE &&
                 fabsf(position.y - counter->position.y) <= MWIN_DOUBLE_CLICK_DISTANCE;
    counter->clicks = quick && counter->clicks < UINT8_MAX ? (uint8_t)(counter->clicks + 1) : 1;
    counter->button = button;
    counter->timeNs = timeNs;
    counter->position = position;
    return counter->clicks;
}

#endif // MAUL_WINDOW_SRC_CLICKS_H
