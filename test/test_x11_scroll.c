// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The wheel movement of XI 2.1 scroll valuators:
// - the first value of a valuator starts the count and moves nothing;
// - a change over the increment, in clicks, fractional: vertical up for
//   a falling value, horizontal right for a rising one;
// - a negative increment turns a valuator round;
// - valuators of other devices, or not for scrolling, move nothing, and
//   an event with none of them is not a scroll;
// - a restart counts from the next value again;
// - valuators of no increment, and past the table, are left out.

#include "test_harness.h"
#include "x11_scroll.h"

// A value as 32.32 fixed point: the whole part rounded down, and the
// fraction above it.
static xcb_input_fp3232_t Fixed(double value)
{
    int32_t whole = (int32_t)value;
    whole -= (double)whole > value ? 1 : 0;
    return (xcb_input_fp3232_t){whole, (uint32_t)((value - (double)whole) * 4294967296.0)};
}

// The movement of an event of a device with valuators 0 to 3 set.
static bool Move(mwinX11Scroll* scroll, uint16_t device, const double values[4], float* x, float* y)
{
    const uint32_t mask = 0xFu;
    xcb_input_fp3232_t fixed[4];
    for (int i = 0; i < 4; i++)
    {
        fixed[i] = Fixed(values[i]);
    }
    return mwinX11Scrolled(scroll, device, &mask, 1, fixed, 4, x, y);
}

int main(void)
{
    mwinX11Scroll scroll = {0};
    // Device 7: valuator 2 vertical by 15, valuator 3 horizontal by -10.
    mwinX11AddScrollAxis(&scroll, 7, 2, false, Fixed(15.0));
    mwinX11AddScrollAxis(&scroll, 7, 3, true, Fixed(-10.0));
    mwinX11AddScrollAxis(&scroll, 7, 1, false, Fixed(0.0));
    float x = 9.0f;
    float y = 9.0f;
    CHECK(Move(&scroll, 7, (const double[4]){5.0, 5.0, 100.0, 50.0}, &x, &y) && x == 0.0f &&
              y == 0.0f,
          "the first value starts the count and moves nothing");
    CHECK(Move(&scroll, 7, (const double[4]){6.0, 6.0, 107.5, 45.0}, &x, &y) && x == 0.5f &&
              y == -0.5f,
          "a change over the increment, fractional, and a negative increment turned round");
    CHECK(Move(&scroll, 7, (const double[4]){6.0, 6.0, 92.5, 65.0}, &x, &y) && x == -2.0f &&
              y == 1.0f,
          "the other way");
    CHECK(!Move(&scroll, 8, (const double[4]){0.0, 0.0, 0.0, 0.0}, &x, &y) && x == 0.0f &&
              y == 0.0f,
          "another device's valuators no scroll");
    const uint32_t pointerOnly = 0x3u;
    const xcb_input_fp3232_t two[2] = {Fixed(1.0), Fixed(2.0)};
    CHECK(!mwinX11Scrolled(&scroll, 7, &pointerOnly, 1, two, 2, &x, &y),
          "valuators not for scrolling no scroll, and one of no increment left out");
    mwinX11RestartScroll(&scroll);
    CHECK(Move(&scroll, 7, (const double[4]){0.0, 0.0, 400.0, 400.0}, &x, &y) && x == 0.0f &&
              y == 0.0f,
          "a restart counts from the next value");
    CHECK(Move(&scroll, 7, (const double[4]){0.0, 0.0, 385.0, 400.0}, &x, &y) && x == 0.0f &&
              y == 1.0f,
          "and on from there");
    for (int i = 0; i < 20; i++)
    {
        mwinX11AddScrollAxis(&scroll, 9, (uint16_t)i, false, Fixed(1.0));
    }
    CHECK(scroll.count == MWIN_X11_SCROLL_AXES, "valuators past the table left out");
    return s_failures == 0 ? 0 : 1;
}
