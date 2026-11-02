// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The name of a document's copy on Android (android_name.h).

#include "android_name.h"

#include "maul-unicode/encoding.h"

#include <stdio.h>
#include <string.h>

static bool IsHigh(uint16_t unit)
{
    return unit >= 0xD800 && unit <= 0xDBFF;
}

static bool IsLow(uint16_t unit)
{
    return unit >= 0xDC00 && unit <= 0xDFFF;
}

void mwinAndroidNameOf(const uint16_t* units, size_t count, char* name)
{
    // Each unit is at least a byte: units past the limit never fit, and
    // the cut below converts no more than the limit's units each time.
    size_t taken = units == nullptr                  ? 0
                   : count > MWIN_ANDROID_NAME_BYTES ? MWIN_ANDROID_NAME_BYTES
                                                     : count;
    size_t needed = 0;
    // Units dropped from the end until the rest fits, never half a pair.
    for (;;)
    {
        (void)muniConvertUtf16ToUtf8(units, taken, muni_convertReplace, nullptr, 0, &needed);
        if (needed <= MWIN_ANDROID_NAME_BYTES || taken == 0)
        {
            break;
        }
        taken -= taken >= 2 && IsLow(units[taken - 1]) && IsHigh(units[taken - 2]) ? 2 : 1;
    }
    (void)muniConvertUtf16ToUtf8(units, taken, muni_convertReplace, name, MWIN_ANDROID_NAME_BYTES,
                                 &needed);
    name[needed] = '\0';
    // A '/' would leave the folder, and a NUL end the name early.
    for (size_t i = 0; i < needed; i++)
    {
        if (name[i] == '/' || name[i] == '\0')
        {
            name[i] = '_';
        }
    }
    if (needed == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        (void)snprintf(name, MWIN_ANDROID_NAME_BYTES + 1, "Document");
    }
}
