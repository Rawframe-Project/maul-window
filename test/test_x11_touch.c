// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Touch on X11 (mwin-0039), without a touch screen: Xvfb has none, so the
// devices and events a driver gives are written out here.
// - a touch begins down, moves and ends up, its id the device's number
//   and the touch's, at the place given;
// - pressure over the valuator's range, clamped, kept per touch from its
//   last event when one leaves the valuator out; a new touch starts with
//   none; -1 for a screen without a pressure valuator or with one of no
//   range;
// - two fingers keep their own pressure; a touch past the kept ones
//   still makes its records, with the pressure its events carry;
// - an update of a touch never begun is a move, and is kept;
// - a device that is no touch screen makes no record, devices past the
//   table are left out, and forgetting the devices keeps the touches;
// - valuators numbered from 0 count, and an event with more values than
//   its mask sets reads no word past the mask.

#include "test_harness.h"
#include "x11_touch.h"

#include <math.h>
#include <string.h>

// A valuator an event leaves out.
#define NONE ((double)NAN)

static xcb_input_fp3232_t Fixed(double value)
{
    int32_t whole = (int32_t)floor(value);
    return (xcb_input_fp3232_t){whole, (uint32_t)((value - (double)whole) * 4294967296.0)};
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

// A screen as the evdev driver makes one: valuators 0 and 1 the place, 2
// the pressure (0 to 255).
static mwinX11TouchDevice* EvdevScreen(mwinX11Touches* touches, uint16_t device)
{
    mwinX11TouchDevice* screen = mwinX11AddTouchDevice(touches, device);
    mwinX11SetTouchPressure(screen, 2, Fixed(0.0), Fixed(255.0));
    return screen;
}

// A touch event of a device with the place valuators and the pressure at
// the value given (NONE leaves it out), and the record it makes.
static int Event(mwinX11Touches* touches, uint16_t device, mwinX11TouchInput input, uint32_t touch,
                 double pressure, mwinEvent* record)
{
    uint32_t mask = 0x3u;
    xcb_input_fp3232_t values[3] = {Fixed(100.0), Fixed(200.0)};
    int count = 2;
    if (!isnan(pressure))
    {
        mask |= 1u << 2;
        values[count++] = Fixed(pressure);
    }
    return mwinX11TouchRecordOf(touches, device, input, touch, (mwinPosition){10.5f, 20.25f}, &mask,
                                1, values, count, record);
}

static void TestTouch(void)
{
    static mwinX11Touches touches;
    touches = (mwinX11Touches){0};
    (void)EvdevScreen(&touches, 9);
    mwinEvent record;
    const uint64_t id = (uint64_t)9 << 32 | 77u;
    CHECK(Event(&touches, 9, mwin_x11TouchBegin, 77, 127.5, &record) == 1 &&
              record.type == mwin_eventTouchDown && record.data.touch.id == id &&
              Near(record.data.touch.pressure, 0.5f) && record.data.touch.position.x == 10.5f &&
              record.data.touch.position.y == 20.25f,
          "down, the id the device's and the touch's, pressure over the range");
    CHECK(Event(&touches, 9, mwin_x11TouchUpdate, 77, NONE, &record) == 1 &&
              record.type == mwin_eventTouchMoved && record.data.touch.id == id &&
              Near(record.data.touch.pressure, 0.5f),
          "a move, its pressure kept when the event leaves it out");
    CHECK(Event(&touches, 9, mwin_x11TouchUpdate, 77, 300.0, &record) == 1 &&
              record.data.touch.pressure == 1.0f &&
              Event(&touches, 9, mwin_x11TouchUpdate, 77, -5.0, &record) == 1 &&
              record.data.touch.pressure == 0.0f,
          "pressure clamped to its range");
    CHECK(Event(&touches, 9, mwin_x11TouchBegin, 78, NONE, &record) == 1 &&
              record.data.touch.pressure == -1.0f &&
              Event(&touches, 9, mwin_x11TouchUpdate, 78, 255.0, &record) == 1 &&
              record.data.touch.pressure == 1.0f &&
              Event(&touches, 9, mwin_x11TouchUpdate, 77, NONE, &record) == 1 &&
              record.data.touch.pressure == 0.0f,
          "a second finger starts with none and keeps its own");
    CHECK(Event(&touches, 9, mwin_x11TouchEnd, 77, NONE, &record) == 1 &&
              record.type == mwin_eventTouchUp && record.data.touch.id == id &&
              record.data.touch.pressure == 0.0f,
          "up, with the last pressure");
    CHECK(Event(&touches, 9, mwin_x11TouchUpdate, 77, NONE, &record) == 1 &&
              record.data.touch.pressure == -1.0f,
          "a touch ended is forgotten");
    CHECK(Event(&touches, 9, mwin_x11TouchBegin, 78, NONE, &record) == 1 &&
              record.data.touch.pressure == -1.0f,
          "a touch begun again, its end lost, starts with no pressure");
}

static void TestDevices(void)
{
    static mwinX11Touches touches;
    touches = (mwinX11Touches){0};
    (void)mwinX11AddTouchDevice(&touches, 4);
    mwinX11TouchDevice* flat = mwinX11AddTouchDevice(&touches, 5);
    mwinX11SetTouchPressure(flat, 2, Fixed(10.0), Fixed(10.0));
    mwinEvent record;
    CHECK(Event(&touches, 4, mwin_x11TouchBegin, 1, 200.0, &record) == 1 &&
              record.data.touch.pressure == -1.0f &&
              Event(&touches, 5, mwin_x11TouchBegin, 1, 20.0, &record) == 1 &&
              record.data.touch.pressure == -1.0f,
          "no pressure valuator, or one of no range: -1");
    CHECK(Event(&touches, 4, mwin_x11TouchBegin, 1, NONE, &record) == 1 &&
              record.data.touch.id != ((uint64_t)5 << 32 | 1u) &&
              record.data.touch.id == ((uint64_t)4 << 32 | 1u),
          "one touch number on two devices, two ids");
    CHECK(Event(&touches, 6, mwin_x11TouchBegin, 1, NONE, &record) == 0,
          "a device that is no touch screen makes no record");
    for (uint16_t device = 10; touches.deviceCount < MWIN_X11_TOUCH_DEVICES; device++)
    {
        (void)mwinX11AddTouchDevice(&touches, device);
    }
    CHECK(mwinX11AddTouchDevice(&touches, 99) == nullptr, "devices past the table left out");
    mwinX11ClearTouchDevices(&touches);
    CHECK(Event(&touches, 4, mwin_x11TouchUpdate, 1, NONE, &record) == 0,
          "devices forgotten make no record");
    (void)EvdevScreen(&touches, 4);
    CHECK(Event(&touches, 4, mwin_x11TouchUpdate, 2, NONE, &record) == 1 &&
              record.type == mwin_eventTouchMoved && record.data.touch.pressure == -1.0f,
          "an update of a touch never begun is a move");
}

static void TestMany(void)
{
    static mwinX11Touches touches;
    touches = (mwinX11Touches){0};
    (void)EvdevScreen(&touches, 3);
    mwinEvent record;
    for (uint32_t touch = 0; touch < MWIN_X11_TOUCHES; touch++)
    {
        (void)Event(&touches, 3, mwin_x11TouchBegin, touch, 255.0, &record);
    }
    CHECK(Event(&touches, 3, mwin_x11TouchBegin, 100, 0.0, &record) == 1 &&
              record.data.touch.pressure == 0.0f &&
              Event(&touches, 3, mwin_x11TouchUpdate, 100, NONE, &record) == 1 &&
              record.data.touch.pressure == -1.0f,
          "a touch past the kept ones: the pressure its events carry");
    CHECK(Event(&touches, 3, mwin_x11TouchUpdate, 0, NONE, &record) == 1 &&
              record.data.touch.pressure == 1.0f,
          "the kept ones keep theirs");
    (void)Event(&touches, 3, mwin_x11TouchEnd, 5, NONE, &record);
    CHECK(Event(&touches, 3, mwin_x11TouchBegin, 101, 0.0, &record) == 1 &&
              Event(&touches, 3, mwin_x11TouchUpdate, 101, NONE, &record) == 1 &&
              record.data.touch.pressure == 0.0f,
          "an ended touch's place taken by a new one");
}

static void TestValuators(void)
{
    static mwinX11Touches touches;
    touches = (mwinX11Touches){0};
    mwinX11TouchDevice* screen = mwinX11AddTouchDevice(&touches, 2);
    mwinX11SetTouchPressure(screen, 0, Fixed(0.0), Fixed(100.0));
    mwinEvent record;
    uint32_t mask = 0x1u;
    const xcb_input_fp3232_t values[2] = {Fixed(25.0), Fixed(90.0)};
    CHECK(mwinX11TouchRecordOf(&touches, 2, mwin_x11TouchBegin, 1, (mwinPosition){0}, &mask, 1,
                               values, 2, &record) == 1 &&
              Near(record.data.touch.pressure, 0.25f),
          "valuator 0 counts");
    mwinX11SetTouchPressure(screen, 33, Fixed(0.0), Fixed(100.0));
    // The second word of the mask sets valuator 33, whose value the event
    // does not carry.
    const uint32_t wide[2] = {0x1u, 0x2u};
    CHECK(mwinX11TouchRecordOf(&touches, 2, mwin_x11TouchBegin, 2, (mwinPosition){0}, wide, 2,
                               values, 1, &record) == 1 &&
              record.data.touch.pressure == -1.0f,
          "no value read past the values given");
    CHECK(mwinX11TouchRecordOf(&touches, 2, mwin_x11TouchBegin, 3, (mwinPosition){0}, wide, 1,
                               values, 2, &record) == 1 &&
              record.data.touch.pressure == -1.0f,
          "no mask word read past the words given");
}

int main(void)
{
    TestTouch();
    TestDevices();
    TestMany();
    TestValuators();
    return s_failures == 0 ? 0 : 1;
}
