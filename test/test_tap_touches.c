// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The touches ios_touch takes from its taps (tap_touches.h): one finger
// per tap at most, every finger down and up.

#include "tap_touches.h"
#include "test_harness.h"

int main(void)
{
    CHECK(TapTouches(1, 1, 1), "one tap, one finger down and up");
    CHECK(!TapTouches(1, 2, 2), "one tap, two fingers down and up");
    CHECK(!TapTouches(1, 0, 0), "one tap, no finger");
    CHECK(!TapTouches(1, 1, 0), "one tap, a finger down and never up");
    CHECK(TapTouches(2, 1, 1) && TapTouches(2, 2, 2),
          "a tap asked for once more: the second's finger, or the first's come late as well");
    CHECK(!TapTouches(2, 3, 3) && !TapTouches(3, 2, 1),
          "more fingers than taps, and a finger down and never up");
    return s_failures == 0 ? 0 : 1;
}
