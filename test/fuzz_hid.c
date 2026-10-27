// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes a HID gamepad's numbering and the reading of its reports
// (hid_controls.c), which take what a device's descriptor and reports
// say through Windows' HID parser. The input is the device's button and
// value capabilities, of any page, usage range, size and logical range,
// then a script of reports: the buttons down, values of any bits for any
// control index, and raw inputs of any size and report count. The
// numbering must stay within its limits with each button once and in
// order; every axis must read between -1 and 1, every hat one of its
// eight steps or centred; a raw input's reports must fit in its bytes
// and its count.

#include "hid_controls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// The most capabilities of each kind the Windows backend reads.
#define CAPS 32

static const uint8_t* s_data;
static size_t s_size;

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

// A capability: the Generic Desktop page, the Button page or another, a
// usage range, and a size and logical range of any value.
static mwinHidCap NextCap(void)
{
    static const uint16_t pages[] = {MWIN_HID_PAGE_GENERIC, MWIN_HID_PAGE_BUTTON, 0xFF00};
    uint8_t shape = Byte();
    mwinHidCap cap = {.page = pages[shape % 3]};
    cap.first = Word();
    // Mostly a short range from the first usage, sometimes any.
    cap.last = (shape & 0x80) != 0 ? Word() : (uint16_t)(cap.first + (Byte() & 15));
    cap.bits = (shape & 0x40) != 0 ? Word() : (uint16_t)(Byte() & 63);
    cap.minimum = (int32_t)Long();
    cap.maximum = (int32_t)Long();
    return cap;
}

static bool IsStep(uint8_t bits)
{
    static const uint8_t steps[] = {0, 1, 3, 2, 6, 4, 12, 8, 9};
    return memchr(steps, bits, sizeof(steps)) != nullptr;
}

static void Check(const mwinHidControls* hid)
{
    const mwinPadControls* controls = &hid->controls;
    Expect(controls->buttonCount <= MWIN_PAD_NUMBERED_BUTTONS &&
           controls->axisCount <= MWIN_PAD_NUMBERED_AXES &&
           controls->hatCount <= MWIN_PAD_NUMBERED_HATS);
    for (uint8_t i = controls->buttonCount; i < MWIN_PAD_NUMBERED_BUTTONS; i++)
    {
        Expect(!controls->buttons[i]);
    }
    for (uint8_t i = 0; i < controls->axisCount; i++)
    {
        float axis = controls->axes[i];
        Expect(isfinite(axis) && axis >= -1.0f && axis <= 1.0f);
    }
    for (uint8_t i = 0; i < controls->hatCount; i++)
    {
        Expect(IsStep(controls->hats[i]));
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    s_data = data;
    s_size = size;
    mwinHidControls hid = {0};
    mwinHidCap caps[CAPS];
    size_t buttons = Byte() % (CAPS + 1);
    for (size_t i = 0; i < buttons; i++)
    {
        caps[i] = NextCap();
    }
    mwinHidAddButtons(&hid, caps, buttons);
    size_t values = Byte() % (CAPS + 1);
    for (size_t i = 0; i < values; i++)
    {
        caps[i] = NextCap();
    }
    mwinHidAddValues(&hid, caps, values);
    // Each button once, in order.
    for (uint8_t i = 1; i < hid.controls.buttonCount; i++)
    {
        Expect(hid.buttons[i - 1] < hid.buttons[i]);
    }
    Check(&hid);
    while (s_size > 0)
    {
        uint8_t op = Byte();
        switch (op % 3)
        {
        case 0:
        {
            uint16_t down[MWIN_PAD_NUMBERED_BUTTONS];
            size_t count = Byte() % (MWIN_PAD_NUMBERED_BUTTONS + 1);
            for (size_t i = 0; i < count; i++)
            {
                down[i] = (op & 0x80) != 0 ? Word() : (uint16_t)(Byte() % 72);
            }
            mwinHidSetButtons(&hid, down, count);
            break;
        }
        case 1:
        {
            uint32_t index = Byte();
            mwinHidSetValue(&hid, index, Long());
            break;
        }
        default:
        {
            size_t bytes = Long();
            uint32_t count = Long();
            uint32_t each = (op & 0x80) != 0 ? Long() : Byte();
            uint32_t reports = mwinHidReports(bytes, count, each);
            Expect(reports <= count && (uint64_t)reports * each <= bytes);
            break;
        }
        }
        Check(&hid);
    }
    return 0;
}
