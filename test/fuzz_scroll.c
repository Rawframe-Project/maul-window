// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the wheel movement of XI 2.1 scroll valuators, as the X server
// and its devices report them: a table of valuators from the input's
// first bytes, then events of a valuator mask and fixed-point values.
// The movement must be finite, and an event of no scroll valuator must
// move nothing.

#include "x11_scroll.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// Takes bytes from the input, or zeros where it ran out.
static void Take(const uint8_t** data, size_t* size, void* out, size_t bytes)
{
    size_t taken = bytes < *size ? bytes : *size;
    memset(out, 0, bytes);
    memcpy(out, *data, taken);
    *data += taken;
    *size -= taken;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    mwinX11Scroll scroll = {0};
    uint8_t axes = 0;
    Take(&data, &size, &axes, 1);
    for (uint8_t i = 0; i < axes % 20; i++)
    {
        struct
        {
            uint16_t device;
            uint16_t number;
            uint8_t horizontal;
            xcb_input_fp3232_t increment;
        } axis;
        Take(&data, &size, &axis, sizeof(axis));
        mwinX11AddScrollAxis(&scroll, axis.device % 4, axis.number % 64, axis.horizontal & 1,
                             axis.increment);
    }
    while (size > 0)
    {
        struct
        {
            uint16_t device;
            uint8_t restart;
            uint32_t mask[2];
            xcb_input_fp3232_t values[8];
        } event;
        Take(&data, &size, &event, sizeof(event));
        if ((event.restart & 1) != 0)
        {
            mwinX11RestartScroll(&scroll);
        }
        float x = 0.0f;
        float y = 0.0f;
        bool scrolled =
            mwinX11Scrolled(&scroll, event.device % 4, event.mask, 2, event.values, 8, &x, &y);
        Expect(isfinite(x) && isfinite(y) && (scrolled || (x == 0.0f && y == 0.0f)));
    }
    return 0;
}
