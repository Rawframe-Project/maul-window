// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text from other programs, made well-formed.

#include "utf8.h"

#include "maul-unicode/encoding.h"

#include <string.h>

size_t mwinRepairUtf8(const char* bytes, size_t length, char* out)
{
    static const char replacement[3] = {'\xEF', '\xBF', '\xBD'};
    size_t written = 0;
    size_t at = 0;
    while (at < length)
    {
        size_t run = muniValidateUtf8(bytes + at, length - at).offset;
        if (out != nullptr && run > 0)
        {
            memcpy(out + written, bytes + at, run);
        }
        written += run;
        at += run;
        if (at == length)
        {
            break;
        }
        uint32_t codePoint = 0;
        size_t size = 1;
        (void)muniDecodeUtf8(bytes + at, length - at, &codePoint, &size);
        if (out != nullptr)
        {
            memcpy(out + written, replacement, sizeof(replacement));
        }
        written += sizeof(replacement);
        at += size;
    }
    return written;
}

mwinResult mwinCheckText(const char* text, size_t length, size_t limit, mwinResult over, bool nul)
{
    if (text == nullptr && length != 0)
    {
        return mwin_errorInvalid;
    }
    if (length > limit)
    {
        return over;
    }
    bool valid = length == 0 || (muniValidateUtf8(text, length).status == muni_success &&
                                 (nul || memchr(text, '\0', length) == nullptr));
    return valid ? mwin_success : mwin_errorInvalid;
}

size_t mwinCutUtf16(const uint16_t* units, size_t count, char* out, size_t capacity)
{
    // The units of the characters that fit, with what each takes in UTF-8.
    size_t taken = 0;
    size_t bytes = 0;
    while (taken < count)
    {
        uint16_t unit = units[taken];
        bool pair = unit >= 0xD800 && unit <= 0xDBFF && taken + 1 < count &&
                    units[taken + 1] >= 0xDC00 && units[taken + 1] <= 0xDFFF;
        size_t size = pair ? 4 : unit < 0x80 ? 1 : unit < 0x800 ? 2 : 3;
        if (size > capacity - bytes)
        {
            break;
        }
        bytes += size;
        taken += pair ? 2 : 1;
    }
    size_t written = 0;
    (void)muniConvertUtf16ToUtf8(units, taken, muni_convertReplace, out, bytes, &written);
    return written;
}
