// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A HID gamepad's controls and reports as plain values (hid_controls.h),
// on every platform:
// - the Button page's usages in order, each once, up to the numbering's
//   limit, other pages left out;
// - the axes from X in usage order whatever the descriptor's, then two
//   sliders, dials or wheels and no third, the hats apart;
// - a range whose maximum reads below its minimum as its bits' unsigned
//   range;
// - a report's buttons down, unknown usages and repeats among them; a
//   signed axis from its bits; a hat's eight steps and a value outside
//   its range centred; ranges as wide as 32 bits;
// - an index past the controls read as nothing;
// - how many reports a raw input holds: as many as fit, never more than
//   it counts, and none of size 0.

#include "hid_controls.h"
#include "test_harness.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bool Near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static mwinHidCap Cap(uint16_t page, uint16_t first, uint16_t last, uint16_t bits, int32_t minimum,
                      int32_t maximum)
{
    return (mwinHidCap){page, first, last, bits, minimum, maximum};
}

static void TestButtons(void)
{
    mwinHidControls hid = {0};
    const mwinHidCap caps[] = {
        Cap(MWIN_HID_PAGE_BUTTON, 3, 3, 1, 0, 1),
        Cap(MWIN_HID_PAGE_BUTTON, 1, 2, 1, 0, 1),
        Cap(MWIN_HID_PAGE_GENERIC, 0x30, 0x30, 8, 0, 255),
        Cap(MWIN_HID_PAGE_BUTTON, 2, 2, 1, 0, 1),
    };
    mwinHidAddButtons(&hid, caps, 4);
    CHECK(hid.controls.buttonCount == 3 && hid.buttons[0] == 1 && hid.buttons[1] == 2 &&
              hid.buttons[2] == 3,
          "the Button page's usages in order, each once, other pages left out");

    const uint16_t down[] = {3, 9, 1, 3};
    mwinHidSetButtons(&hid, down, 4);
    CHECK(hid.controls.buttons[0] && !hid.controls.buttons[1] && hid.controls.buttons[2],
          "a report's buttons down, an unknown usage and a repeat among them");
    mwinHidSetButtons(&hid, down, 0);
    CHECK(!hid.controls.buttons[0] && !hid.controls.buttons[2], "a report of none up");

    mwinHidControls many = {0};
    const mwinHidCap range = Cap(MWIN_HID_PAGE_BUTTON, 1, 100, 1, 0, 1);
    mwinHidAddButtons(&many, &range, 1);
    CHECK(many.controls.buttonCount == MWIN_PAD_NUMBERED_BUTTONS &&
              many.buttons[MWIN_PAD_NUMBERED_BUTTONS - 1] == MWIN_PAD_NUMBERED_BUTTONS,
          "buttons up to the numbering's limit");
}

static void TestValues(void)
{
    mwinHidControls hid = {0};
    // In the descriptor's order: X, Y, Z, Rz, a hat, Rx, Ry, a vendor's,
    // a slider, a dial, a wheel; the wheel is a third slider.
    const mwinHidCap caps[] = {
        Cap(MWIN_HID_PAGE_GENERIC, 0x30, 0x32, 8, 0, 255),
        Cap(MWIN_HID_PAGE_GENERIC, 0x35, 0x35, 8, -128, 127),
        Cap(MWIN_HID_PAGE_GENERIC, 0x39, 0x39, 4, 0, 7),
        Cap(MWIN_HID_PAGE_GENERIC, 0x33, 0x34, 8, 0, 255),
        Cap(0xFF00, 0x20, 0x20, 8, 0, 255),
        Cap(MWIN_HID_PAGE_GENERIC, 0x36, 0x38, 8, 0, -1),
    };
    mwinHidAddValues(&hid, caps, 6);
    static const uint16_t order[] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};
    bool ordered = hid.controls.axisCount == 8;
    for (int i = 0; ordered && i < 8; i++)
    {
        ordered = hid.axes[i].usage == order[i];
    }
    CHECK(ordered && hid.controls.hatCount == 1 && hid.hats[0].usage == 0x39,
          "the axes from X in usage order, two sliders and no third, the hat apart");
    CHECK(hid.axes[6].minimum == 0 && hid.axes[6].maximum == 255,
          "a range whose maximum reads below its minimum as its bits' unsigned range");

    mwinHidSetValue(&hid, 0, 255);
    mwinHidSetValue(&hid, 1, 0);
    mwinHidSetValue(&hid, 5, 0x80);
    float rz = hid.controls.axes[5];
    mwinHidSetValue(&hid, 5, 0x7F);
    CHECK(Near(hid.controls.axes[0], 1.0f) && Near(hid.controls.axes[1], -1.0f) &&
              Near(rz, -1.0f) && Near(hid.controls.axes[5], 1.0f),
          "axes over their ranges, a signed one from its bits");

    static const uint8_t steps[8] = {1, 3, 2, 6, 4, 12, 8, 9};
    bool stepped = true;
    for (uint32_t value = 0; value < 8; value++)
    {
        mwinHidSetValue(&hid, 8, value);
        stepped = stepped && hid.controls.hats[0] == steps[value];
    }
    mwinHidSetValue(&hid, 8, 8);
    CHECK(stepped && hid.controls.hats[0] == 0,
          "a hat's eight steps clockwise from up, and a value outside its range centred");

    mwinHidControls before = hid;
    mwinHidSetValue(&hid, 9, 1);
    mwinHidSetValue(&hid, UINT32_MAX, 1);
    CHECK(memcmp(&before, &hid, sizeof(hid)) == 0, "an index past the controls read as nothing");
}

// Ranges as wide as 32 bits.
static void TestWide(void)
{
    mwinHidControls hid = {0};
    const mwinHidCap caps[] = {
        Cap(MWIN_HID_PAGE_GENERIC, 0x30, 0x30, 32, INT32_MIN, INT32_MAX),
        Cap(MWIN_HID_PAGE_GENERIC, 0x39, 0x39, 32, INT32_MIN, INT32_MAX),
    };
    mwinHidAddValues(&hid, caps, 2);
    mwinHidSetValue(&hid, 0, (uint32_t)INT32_MIN);
    float least = hid.controls.axes[0];
    mwinHidSetValue(&hid, 0, (uint32_t)INT32_MAX);
    CHECK(Near(least, -1.0f) && Near(hid.controls.axes[0], 1.0f),
          "an axis whose range spans 32 bits");
    mwinHidSetValue(&hid, 1, 0);
    uint8_t middle = hid.controls.hats[0];
    mwinHidSetValue(&hid, 1, (uint32_t)INT32_MIN);
    uint8_t first = hid.controls.hats[0];
    mwinHidSetValue(&hid, 1, (uint32_t)INT32_MAX);
    CHECK(middle == 4 && first == 1 && hid.controls.hats[0] == 9,
          "a hat whose range spans 32 bits: down at its middle, up and up-left at its ends");
}

static void TestReports(void)
{
    CHECK(mwinHidReports(100, 3, 10) == 3 && mwinHidReports(25, 3, 10) == 2 &&
              mwinHidReports(9, 3, 10) == 0,
          "as many reports as fit, never more than the input counts");
    CHECK(mwinHidReports(100, 5, 0) == 0 && mwinHidReports(0, 5, 0) == 0, "none of size 0");
    CHECK(mwinHidReports(SIZE_MAX, UINT32_MAX, 1) == UINT32_MAX,
          "the most an input counts, when they fit");
}

int main(void)
{
    TestButtons();
    TestValues();
    TestWide();
    TestReports();
    return s_failures == 0 ? 0 : 1;
}
