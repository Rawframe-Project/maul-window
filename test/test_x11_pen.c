// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pens on X11 (mwin-0037), without a tablet: Xvfb has none, so the
// devices and events a driver gives are written out here.
// - a device is a pen by its X input type, STYLUS or ERASER; one of
//   type TABLET or none by an eraser in its name, in any case, else by a
//   pressure valuator; a STYLUS named an eraser is the eraser; a device
//   of another type, as a touch screen with pressure, is none;
// - hovering moves with no pressure, tilt in degrees and clamped;
// - the tip's press and release are down and up, pressure over the
//   valuator's range while in contact, kept from the last event when an
//   event leaves the valuator out;
// - button 2 is the barrel, told while in contact; button 3 makes no
//   record;
// - a pen without a pressure valuator presses fully;
// - devices past the table are left out, and only pens are found.

#include "test_harness.h"
#include "x11_pen.h"

#include <math.h>
#include <string.h>

#define STYLUS      101
#define ERASER      102
#define TABLET      103
#define TOUCHSCREEN 104
// A valuator an event leaves out.
#define NONE ((double)NAN)

static const mwinX11PenTypes s_types = {STYLUS, ERASER, TABLET};

static xcb_input_fp3232_t Fixed(double value)
{
    int32_t whole = (int32_t)floor(value);
    return (xcb_input_fp3232_t){whole, (uint32_t)((value - (double)whole) * 4294967296.0)};
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

// A pen as the Wacom driver makes one: valuators 0 and 1 the place, 2
// the pressure (0 to 2047), 3 and 4 the tilt (-64 to 63).
static mwinX11Pen* WacomPen(mwinX11Pens* pens, uint16_t device, bool eraser)
{
    mwinX11Pen* pen = mwinX11AddPen(pens, device, eraser);
    mwinX11SetPenAxis(pen, mwin_x11PenPressure, 2, Fixed(0.0), Fixed(2047.0));
    mwinX11SetPenAxis(pen, mwin_x11PenTiltX, 3, Fixed(-64.0), Fixed(63.0));
    mwinX11SetPenAxis(pen, mwin_x11PenTiltY, 4, Fixed(-64.0), Fixed(63.0));
    return pen;
}

// An event of a pen with valuators 2 to 4 at the values given (a NONE
// leaves that one out), and the record it makes.
static int Event(mwinX11Pen* pen, mwinX11PenInput input, uint32_t button, double pressure,
                 double tiltX, double tiltY, mwinEvent* record)
{
    const double given[3] = {pressure, tiltX, tiltY};
    uint32_t mask = 0;
    xcb_input_fp3232_t values[3];
    int count = 0;
    for (int i = 0; i < 3; i++)
    {
        if (!isnan(given[i]))
        {
            mask |= 1u << (2 + i);
            values[count++] = Fixed(given[i]);
        }
    }
    return mwinX11PenRecordOf(pen, input, button, (mwinPosition){10.5f, 20.25f}, &mask, 1, values,
                              count, record);
}

static void TestKinds(void)
{
    static const char stylus[] = "Wacom Intuos S Pen stylus";
    static const char eraser[] = "Wacom Intuos S Pen eraser";
    static const char upper[] = "Tablet ERASER";
    static const char mouse[] = "Logitech USB Mouse";
    CHECK(mwinX11PenKindOf(STYLUS, s_types, stylus, strlen(stylus), true) == mwin_x11Stylus &&
              mwinX11PenKindOf(ERASER, s_types, mouse, strlen(mouse), false) == mwin_x11Eraser,
          "the X input type names a stylus and an eraser");
    CHECK(mwinX11PenKindOf(STYLUS, s_types, eraser, strlen(eraser), true) == mwin_x11Eraser &&
              mwinX11PenKindOf(XCB_ATOM_NONE, s_types, upper, strlen(upper), false) ==
                  mwin_x11Eraser,
          "an eraser in the name, in any case");
    CHECK(mwinX11PenKindOf(TABLET, s_types, stylus, strlen(stylus), true) == mwin_x11Stylus &&
              mwinX11PenKindOf(TABLET, s_types, mouse, strlen(mouse), false) == mwin_x11NotPen &&
              mwinX11PenKindOf(XCB_ATOM_NONE, s_types, mouse, strlen(mouse), false) ==
                  mwin_x11NotPen,
          "else a pressure valuator makes a pen, and a mouse is none");
    CHECK(mwinX11PenKindOf(XCB_ATOM_NONE, s_types, "erase", 5, false) == mwin_x11NotPen,
          "a part of the word is no eraser");
    static const char screen[] = "eGalax touch screen eraser";
    CHECK(mwinX11PenKindOf(TOUCHSCREEN, s_types, stylus, strlen(stylus), true) == mwin_x11NotPen &&
              mwinX11PenKindOf(TOUCHSCREEN, s_types, screen, strlen(screen), true) ==
                  mwin_x11NotPen,
          "a touch screen with pressure, or any name, is no pen");
}

static void TestStroke(void)
{
    mwinX11Pens pens = {0};
    mwinX11Pen* pen = WacomPen(&pens, 12, false);
    mwinEvent record;
    CHECK(Event(pen, mwin_x11PenMotion, 0, 300.0, 20.0, -80.0, &record) == 1 &&
              record.type == mwin_eventPenMoved && record.data.pen.pressure == 0.0f &&
              Near(record.data.pen.tiltX, 20.0f) && record.data.pen.tiltY == -80.0f &&
              record.data.pen.flags == 0 && Near(record.data.pen.position.x, 10.5f),
          "hovering: no pressure, tilt in degrees");
    CHECK(Event(pen, mwin_x11PenMotion, 0, NONE, 120.0, NONE, &record) == 1 &&
              record.data.pen.tiltX == 90.0f && record.data.pen.tiltY == -80.0f,
          "tilt clamped to 90 degrees, the other kept");
    CHECK(Event(pen, mwin_x11PenPress, 1, 1023.5, NONE, NONE, &record) == 1 &&
              record.type == mwin_eventPenDown && record.data.pen.flags == mwin_penContact &&
              Near(record.data.pen.pressure, 0.5f),
          "the tip down, pressure over the valuator's range");
    CHECK(Event(pen, mwin_x11PenMotion, 0, NONE, NONE, NONE, &record) == 1 &&
              Near(record.data.pen.pressure, 0.5f),
          "a value left out is kept");
    CHECK(Event(pen, mwin_x11PenPress, 2, NONE, NONE, NONE, &record) == 1 &&
              record.type == mwin_eventPenButtonDown && record.data.pen.button == 1 &&
              record.data.pen.flags == (mwin_penContact | mwin_penBarrel),
          "button 2 the barrel, while in contact");
    CHECK(Event(pen, mwin_x11PenPress, 3, NONE, NONE, NONE, &record) == 0 &&
              Event(pen, mwin_x11PenRelease, 3, NONE, NONE, NONE, &record) == 0,
          "button 3 makes no record");
    CHECK(Event(pen, mwin_x11PenRelease, 2, NONE, NONE, NONE, &record) == 1 &&
              record.type == mwin_eventPenButtonUp && record.data.pen.flags == mwin_penContact,
          "the barrel released");
    CHECK(Event(pen, mwin_x11PenRelease, 1, 0.0, NONE, NONE, &record) == 1 &&
              record.type == mwin_eventPenUp && record.data.pen.flags == 0 &&
              record.data.pen.pressure == 0.0f,
          "the tip up");
    CHECK(Event(pen, mwin_x11PenPress, 1, 4000.0, NONE, NONE, &record) == 1 &&
              record.data.pen.pressure == 1.0f,
          "pressure past the range clamped");
}

static void TestBare(void)
{
    mwinX11Pens pens = {0};
    mwinX11Pen* eraser = mwinX11AddPen(&pens, 5, true);
    mwinEvent record;
    CHECK(Event(eraser, mwin_x11PenMotion, 0, 900.0, 30.0, 30.0, &record) == 1 &&
              record.data.pen.flags == mwin_penEraser && record.data.pen.tiltX == 0.0f &&
              record.data.pen.tiltY == 0.0f,
          "an eraser without valuators: its flag, no tilt");
    CHECK(Event(eraser, mwin_x11PenPress, 1, NONE, NONE, NONE, &record) == 1 &&
              record.data.pen.pressure == 1.0f &&
              record.data.pen.flags == (mwin_penEraser | mwin_penContact),
          "without a pressure valuator it presses fully");
}

static void TestTable(void)
{
    mwinX11Pens pens = {0};
    for (uint16_t i = 0; i < MWIN_X11_PENS; i++)
    {
        CHECK(mwinX11AddPen(&pens, (uint16_t)(20 + i), false) != nullptr, "a pen in the table");
    }
    CHECK(mwinX11AddPen(&pens, 99, false) == nullptr, "past the table, left out");
    CHECK(mwinX11FindPen(&pens, 23) == &pens.pens[3] && mwinX11FindPen(&pens, 99) == nullptr &&
              mwinX11FindPen(&pens, 2) == nullptr,
          "only pens are found");
}

int main(void)
{
    TestKinds();
    TestStroke();
    TestBare();
    TestTable();
    return s_failures == 0 ? 0 : 1;
}
