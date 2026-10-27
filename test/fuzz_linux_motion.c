// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the reading of a Linux gamepad's motion device
// (linux_motion_events.c), which takes what the device hands over: two
// devices' identities, the axes' values and resolutions, then a script
// of evdev events at times of any spacing: axes, stamps, reports, the
// kernel's drops, and events of other types and codes, with the axes
// read again wherever the reading asks. Matching must be symmetric, and
// a device match itself where it has a unique id or a path; an
// axis must take only a resolution above 0; a report must give a sample,
// or a resync where it ends a report the kernel cut, and nothing else
// may; a sample must hold each axis's last value read outside a cut
// report, finite and in the contract's frame; a report without a stamp
// must read at its event time, and a stamped one within 100 ms of it.

#include "linux_motion_events.h"

#include <linux/input.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// How far a stamped time may stray from its event time.
#define STRAY_NS           100000000u
#define STANDARD_GRAVITY   9.80665f
#define RADIANS_PER_DEGREE 0.017453292f

static const uint8_t* s_data;
static size_t s_size;
// Each axis's last value and factor, kept apart from the reading.
static int32_t s_raw[MWIN_LINUX_MOTION_AXES];
static float s_scale[MWIN_LINUX_MOTION_AXES];

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static uint8_t Byte(void)
{
    if (s_size == 0)
    {
        return 0;
    }
    s_size--;
    return *s_data++;
}

static uint16_t Word(void)
{
    uint16_t low = Byte();
    return (uint16_t)(low | (uint16_t)Byte() << 8);
}

static uint32_t Long(void)
{
    uint32_t low = Word();
    return low | (uint32_t)Word() << 16;
}

// A text field: as many bytes as the input says, up to filling it with
// no end.
static void Field(char* field, size_t size)
{
    size_t length = Byte() % (size + 1);
    for (size_t i = 0; i < length; i++)
    {
        field[i] = (char)Byte();
    }
}

static mwinLinuxPadIdentity NextIdentity(void)
{
    mwinLinuxPadIdentity identity = {0};
    Field(identity.uniq, sizeof(identity.uniq));
    Field(identity.phys, sizeof(identity.phys));
    identity.vendor = Word();
    return identity;
}

// The axes' values and resolutions, as the device gives them: read until
// one is refused, as the reader stops there.
static void ReadAxes(mwinLinuxMotionEvents* events)
{
    for (unsigned axis = 0; axis < MWIN_LINUX_MOTION_AXES; axis++)
    {
        int32_t value = (int32_t)Long();
        int32_t resolution = (int32_t)Long();
        bool taken = mwinLinuxMotionAxis(events, axis, value, resolution);
        Expect(taken == (resolution > 0));
        if (!taken)
        {
            return;
        }
        s_raw[axis] = value;
        s_scale[axis] = (axis < 3 ? STANDARD_GRAVITY : RADIANS_PER_DEGREE) / (float)resolution;
    }
}

// The sample the axes' last values make, in the contract's frame:
// Nintendo's axes reordered.
static void CheckSample(const mwinLinuxMotionSample* sample, bool nintendo)
{
    float values[MWIN_LINUX_MOTION_AXES];
    for (int i = 0; i < MWIN_LINUX_MOTION_AXES; i++)
    {
        values[i] = (float)s_raw[i] * s_scale[i];
        Expect(isfinite(values[i]));
    }
    const int order[MWIN_LINUX_MOTION_AXES] = {1, 2, 0, 4, 5, 3};
    const float sign[MWIN_LINUX_MOTION_AXES] = {-1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f};
    for (int i = 0; i < 3; i++)
    {
        float acceleration = nintendo ? sign[i] * values[order[i]] : values[i];
        float rate = nintendo ? sign[3 + i] * values[order[3 + i]] : values[3 + i];
        Expect(sample->acceleration[i] == acceleration && sample->rate[i] == rate);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    s_data = data;
    s_size = size;
    memset(s_raw, 0, sizeof(s_raw));
    memset(s_scale, 0, sizeof(s_scale));
    mwinLinuxPadIdentity pad = NextIdentity();
    mwinLinuxPadIdentity device = NextIdentity();
    Expect(mwinLinuxSameController(&pad, &device) == mwinLinuxSameController(&device, &pad));
    Expect(mwinLinuxSameController(&pad, &pad) == (pad.uniq[0] != '\0' || pad.phys[0] != '\0'));
    mwinLinuxMotionEvents events;
    mwinLinuxMotionStart(&events, &pad);
    Expect(!mwinLinuxMotionAxis(&events, MWIN_LINUX_MOTION_AXES, 0, 1));
    ReadAxes(&events);

    // What the reading should do, kept apart from it: whether it skips
    // since a drop, and whether the report under way has a stamp.
    bool dropping = false;
    bool stamped = false;
    uint64_t timeNs = (uint64_t)Long() << 32;
    timeNs |= Long();
    while (s_size > 0)
    {
        uint8_t op = Byte();
        uint16_t type;
        uint16_t code;
        int32_t value = 0;
        switch (op % 4)
        {
        case 0:
        {
            // Mostly one of the six axes, sometimes any code.
            unsigned axis = (op >> 2) % MWIN_LINUX_MOTION_AXES;
            type = EV_ABS;
            code =
                (op & 0x80) != 0 ? Word() : (uint16_t)(axis < 3 ? ABS_X + axis : ABS_RX + axis - 3);
            value = (int32_t)Long();
            break;
        }
        case 1:
            type = EV_MSC;
            code = (op & 0x80) != 0 ? Word() : MSC_TIMESTAMP;
            value = (int32_t)Long();
            break;
        case 2:
            type = EV_SYN;
            code = (op & 0x80) != 0 ? SYN_DROPPED : (op & 0x40) != 0 ? Word() : SYN_REPORT;
            timeNs += (op & 0x20) != 0 ? (uint64_t)Long() * 1000u : (uint64_t)Word() * 1000u;
            break;
        default:
            type = Word();
            code = Word();
            value = (int32_t)Long();
            break;
        }

        bool report = type == EV_SYN && code == SYN_REPORT;
        bool drop = type == EV_SYN && code == SYN_DROPPED;
        mwinLinuxMotionSample sample;
        mwinLinuxMotionStep step =
            mwinLinuxMotionEvent(&events, type, code, value, timeNs, &sample);
        if (drop || (dropping && !report))
        {
            Expect(step == mwin_motionNothing);
            dropping = true;
            stamped = false;
            continue;
        }
        if (dropping)
        {
            Expect(step == mwin_motionResync);
            dropping = false;
            stamped = false;
            ReadAxes(&events);
            continue;
        }
        if (!report)
        {
            Expect(step == mwin_motionNothing);
            stamped |= type == EV_MSC && code == MSC_TIMESTAMP;
            if (type == EV_ABS && code <= ABS_Z)
            {
                s_raw[code - ABS_X] = value;
            }
            else if (type == EV_ABS && code >= ABS_RX && code <= ABS_RZ)
            {
                s_raw[3 + code - ABS_RX] = value;
            }
            continue;
        }
        Expect(step == mwin_motionSample);
        CheckSample(&sample, pad.vendor == 0x057E);
        if (stamped)
        {
            uint64_t stray =
                sample.timeNs > timeNs ? sample.timeNs - timeNs : timeNs - sample.timeNs;
            Expect(stray <= STRAY_NS);
        }
        else
        {
            Expect(sample.timeNs == timeNs);
        }
        stamped = false;
    }
    return 0;
}
