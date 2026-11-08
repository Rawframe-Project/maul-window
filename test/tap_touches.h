// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the taps ios_touch asks idb for may bring: each tap one finger,
// down and up, so as many ups as downs, at least one, and no more than
// the taps sent. A single tap demands exactly one down and one up; two
// fingers from one tap fail. test_tap_touches checks it on every
// platform, since the simulator's taps run only in CI.

#ifndef MAUL_WINDOW_TEST_TAP_TOUCHES_H
#define MAUL_WINDOW_TEST_TAP_TOUCHES_H

#include <stdbool.h>

static inline bool TapTouches(int taps, int downs, int ups)
{
    return downs == ups && downs >= 1 && downs <= taps;
}

#endif // MAUL_WINDOW_TEST_TAP_TOUCHES_H
