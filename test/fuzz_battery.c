// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the reading of a Linux gamepad's battery charge
// (mwinLinuxBatteryPercent in linux_battery.c) from the text of the
// kernel's capacity file, of any bytes and length. The charge must be
// what the header says, worked out here apart from the reader: a number
// of one to three digits from 0 to 100, ending the text or followed by a
// newline or a NUL, whatever comes after that; else -1. The text is
// handed over as the fuzzer holds it, with nothing after its last byte,
// so a read past its length is a sanitizer's finding.

#include "linux_battery.h"

#include <stdlib.h>

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static int Expected(const uint8_t* data, size_t size)
{
    size_t digits = 0;
    while (digits < size && data[digits] >= '0' && data[digits] <= '9')
    {
        digits++;
    }
    if (digits == 0 || digits > 3 ||
        (digits < size && data[digits] != '\n' && data[digits] != '\0'))
    {
        return -1;
    }
    int value = 0;
    for (size_t i = 0; i < digits; i++)
    {
        value = value * 10 + (data[i] - '0');
    }
    return value <= 100 ? value : -1;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Expect(mwinLinuxBatteryPercent((const char*)data, size) == Expected(data, size));
    return 0;
}
