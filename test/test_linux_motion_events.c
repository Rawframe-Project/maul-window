// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's motion device as plain values (linux_motion_events.h),
// on Linux without a device:
// - which pad a device belongs to: the unique id where both have one,
//   whatever their paths; else the same path, and never an empty one;
//   a unique id filling its whole field compared within it;
// - an axis's factor from its resolution, to m/s^2 per unit for
//   acceleration and rad/s per unit for rotation; a missing or negative
//   resolution, or an axis past the six, refused;
// - a sample only at a report, holding every axis's last value, other
//   events left out; Nintendo's axes reordered into the contract's frame;
// - a report without a stamp at its event time; stamped reports from the
//   first one's event time, spaced as their stamps say, across a wrap;
//   a stamped time straying over 100 ms from the event time started
//   again from it; a report without a stamp among stamped ones at its
//   own event time, the stamps after it still counted from the last.

#include "linux_motion_events.h"
#include "test_harness.h"

#include <linux/input.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define MS 1000000u
// One g, and one degree per second, in the contract's units.
#define G 9.80665f
#define D 0.017453292f

static bool Near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static mwinLinuxPadIdentity Identity(const char* uniq, const char* phys, uint16_t vendor)
{
    mwinLinuxPadIdentity identity = {.vendor = vendor};
    (void)strncpy(identity.uniq, uniq, sizeof(identity.uniq) - 1);
    (void)strncpy(identity.phys, phys, sizeof(identity.phys) - 1);
    return identity;
}

// A reading with every axis's resolution 1, so a raw value counts g or
// degrees per second.
static mwinLinuxMotionEvents Start(uint16_t vendor)
{
    mwinLinuxPadIdentity pad = Identity("", "", vendor);
    mwinLinuxMotionEvents events;
    mwinLinuxMotionStart(&events, &pad);
    for (unsigned i = 0; i < MWIN_LINUX_MOTION_AXES; i++)
    {
        (void)mwinLinuxMotionAxis(&events, i, 0, 1);
    }
    return events;
}

// Ends a report at a time, with a stamp unless it is negative: the
// sample's time, or 0 when no sample came.
static uint64_t Report(mwinLinuxMotionEvents* events, int64_t stamp, uint64_t timeNs)
{
    mwinLinuxMotionSample sample = {0};
    if (stamp >= 0)
    {
        (void)mwinLinuxMotionEvent(events, EV_MSC, MSC_TIMESTAMP, (int32_t)(uint32_t)stamp, timeNs,
                                   &sample);
    }
    return mwinLinuxMotionEvent(events, EV_SYN, SYN_REPORT, 0, timeNs, &sample) ? sample.timeNs : 0;
}

static void TestController(void)
{
    mwinLinuxPadIdentity pad = Identity("aa:bb", "usb-1/input0", 0x054C);
    mwinLinuxPadIdentity same = Identity("aa:bb", "usb-1/input1", 0x054C);
    mwinLinuxPadIdentity other = Identity("cc:dd", "usb-1/input0", 0x054C);
    CHECK(mwinLinuxSameController(&pad, &same), "the same unique id, whatever the paths");
    CHECK(!mwinLinuxSameController(&pad, &other),
          "another unique id on the same path, as Bluetooth pads share the adapter's");

    mwinLinuxPadIdentity bare = Identity("", "usb-1/input0", 0x054C);
    CHECK(!mwinLinuxSameController(&pad, &bare) && !mwinLinuxSameController(&bare, &pad),
          "a unique id on one side only");
    mwinLinuxPadIdentity bareSame = Identity("", "usb-1/input0", 0x054C);
    mwinLinuxPadIdentity bareOther = Identity("", "usb-2/input0", 0x054C);
    CHECK(mwinLinuxSameController(&bare, &bareSame) && !mwinLinuxSameController(&bare, &bareOther),
          "without unique ids, the same path");
    mwinLinuxPadIdentity empty = Identity("", "", 0x054C);
    CHECK(!mwinLinuxSameController(&empty, &empty), "never an empty path");

    mwinLinuxPadIdentity full = pad;
    memset(full.uniq, 'u', sizeof(full.uniq));
    mwinLinuxPadIdentity fullSame = full;
    memset(fullSame.phys, 'p', sizeof(fullSame.phys));
    CHECK(mwinLinuxSameController(&full, &fullSame),
          "a unique id filling its whole field, compared within it");
}

static void TestAxes(void)
{
    mwinLinuxPadIdentity pad = Identity("", "", 0x054C);
    mwinLinuxMotionEvents events;
    mwinLinuxMotionStart(&events, &pad);
    CHECK(!mwinLinuxMotionAxis(&events, 0, 5, 0) && !mwinLinuxMotionAxis(&events, 3, 5, -4) &&
              !mwinLinuxMotionAxis(&events, MWIN_LINUX_MOTION_AXES, 5, 1),
          "a missing or negative resolution, or an axis past the six, refused");

    CHECK(mwinLinuxMotionAxis(&events, 0, 8192, 8192) &&
              mwinLinuxMotionAxis(&events, 1, -4096, 8192) &&
              mwinLinuxMotionAxis(&events, 2, 0, 8192) &&
              mwinLinuxMotionAxis(&events, 3, 1024, 1024) &&
              mwinLinuxMotionAxis(&events, 4, 0, 1024) && mwinLinuxMotionAxis(&events, 5, 0, 1024),
          "every axis with a resolution");
    mwinLinuxMotionSample sample = {0};
    CHECK(mwinLinuxMotionEvent(&events, EV_SYN, SYN_REPORT, 0, 5 * MS, &sample) &&
              Near(sample.acceleration[0], 9.80665f) && Near(sample.acceleration[1], -4.903325f) &&
              Near(sample.rate[0], 0.017453292f),
          "a resolution's units as one g and one degree per second, the axes' current values");
}

static void TestReports(void)
{
    mwinLinuxMotionEvents events = Start(0x054C);
    mwinLinuxMotionSample sample = {0};
    bool early = false;
    const struct
    {
        uint16_t type;
        uint16_t code;
        int32_t value;
    } stream[] = {
        {EV_ABS, ABS_X, 1},    {EV_ABS, ABS_Y, 2},       {EV_ABS, ABS_Z, 3},
        {EV_ABS, ABS_RX, 4},   {EV_ABS, ABS_RY, 5},      {EV_ABS, ABS_RZ, 6},
        {EV_KEY, BTN_A, 1},    {EV_ABS, ABS_HAT0X, 9},   {EV_ABS, ABS_MAX, 9},
        {EV_MSC, MSC_SCAN, 9}, {EV_SYN, SYN_DROPPED, 0}, {EV_REL, REL_X, 9},
    };
    for (size_t i = 0; i < sizeof(stream) / sizeof(stream[0]); i++)
    {
        early |= mwinLinuxMotionEvent(&events, stream[i].type, stream[i].code, stream[i].value, MS,
                                      &sample);
    }
    CHECK(!early, "no sample before a report");
    CHECK(mwinLinuxMotionEvent(&events, EV_SYN, SYN_REPORT, 0, MS, &sample) &&
              Near(sample.acceleration[0], 1 * G) && Near(sample.acceleration[1], 2 * G) &&
              Near(sample.acceleration[2], 3 * G) && Near(sample.rate[0], 4 * D) &&
              Near(sample.rate[1], 5 * D) && Near(sample.rate[2], 6 * D),
          "a report's sample in the device's order, other events left out");
    (void)mwinLinuxMotionEvent(&events, EV_ABS, ABS_Y, -7, 2 * MS, &sample);
    CHECK(mwinLinuxMotionEvent(&events, EV_SYN, SYN_REPORT, 0, 2 * MS, &sample) &&
              Near(sample.acceleration[0], 1 * G) && Near(sample.acceleration[1], -7 * G) &&
              Near(sample.rate[2], 6 * D),
          "a report holding the axes it left out");

    mwinLinuxMotionEvents nintendo = Start(0x057E);
    for (uint16_t code = ABS_X; code <= ABS_Z; code++)
    {
        (void)mwinLinuxMotionEvent(&nintendo, EV_ABS, code, code + 1, MS, &sample);
        (void)mwinLinuxMotionEvent(&nintendo, EV_ABS, ABS_RX + code, code + 4, MS, &sample);
    }
    CHECK(mwinLinuxMotionEvent(&nintendo, EV_SYN, SYN_REPORT, 0, MS, &sample) &&
              Near(sample.acceleration[0], -2 * G) && Near(sample.acceleration[1], 3 * G) &&
              Near(sample.acceleration[2], -1 * G) && Near(sample.rate[0], -5 * D) &&
              Near(sample.rate[1], 6 * D) && Near(sample.rate[2], -4 * D),
          "Nintendo's axes reordered into the contract's frame");

    mwinLinuxMotionEvents extreme = Start(0x054C);
    (void)mwinLinuxMotionAxis(&extreme, 0, INT32_MIN, 1);
    (void)mwinLinuxMotionAxis(&extreme, 3, INT32_MAX, 1);
    CHECK(mwinLinuxMotionEvent(&extreme, EV_SYN, SYN_REPORT, 0, MS, &sample) &&
              isfinite(sample.acceleration[0]) && isfinite(sample.rate[0]),
          "the widest values finite");
}

static void TestTimes(void)
{
    mwinLinuxMotionEvents events = Start(0x054C);
    CHECK(Report(&events, -1, 7 * MS) == 7 * MS, "a report without a stamp at its event time");
    CHECK(Report(&events, 1000, 9 * MS) == 9 * MS, "the first stamped report at its event time");
    CHECK(Report(&events, 5000, 15 * MS) == 13 * MS && Report(&events, 9000, 16 * MS) == 17 * MS,
          "stamped reports spaced as their stamps say, whatever their event times");
    CHECK(Report(&events, -1, 18 * MS) == 18 * MS,
          "a report without a stamp among stamped ones at its own event time");
    CHECK(Report(&events, 13000, 20 * MS) == 21 * MS,
          "the stamps after it still counted from the last");

    mwinLinuxMotionEvents wrap = Start(0x054C);
    (void)Report(&wrap, 0xFFFFFC18, 100 * MS);
    CHECK(Report(&wrap, 1000, 101 * MS) == 102 * MS, "stamps across a wrap");

    mwinLinuxMotionEvents stray = Start(0x054C);
    (void)Report(&stray, 0, 100 * MS);
    CHECK(Report(&stray, 100000, 200 * MS) == 200 * MS &&
              Report(&stray, 300000, 300 * MS) == 400 * MS,
          "a stamped time up to 100 ms from the event time kept");
    CHECK(Report(&stray, 500000, 350 * MS) == 350 * MS &&
              Report(&stray, 510000, 361 * MS) == 360 * MS,
          "a stamped time straying over 100 ms started again from the event time");
    CHECK(Report(&stray, 200000, 380 * MS) == 380 * MS,
          "a stamp going back started again from the event time");
}

int main(void)
{
    TestController();
    TestAxes();
    TestReports();
    TestTimes();
    return s_failures == 0 ? 0 : 1;
}
