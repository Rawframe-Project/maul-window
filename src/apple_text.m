// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text helpers the Apple backends share (apple_text.h).

#include "apple_text.h"

#include "allocator.h"

#include "maul-unicode/encoding.h"

// The units of a string in a block of the context's, of *bytesOut bytes;
// NULL for an empty string, or with no memory.
static uint16_t* Units(mwinContext* context, NSString* string, size_t* bytesOut)
{
    NSUInteger length = string.length;
    *bytesOut = 0;
    if (length == 0 || length > SIZE_MAX / sizeof(uint16_t))
    {
        return nullptr;
    }
    size_t bytes = (size_t)length * sizeof(uint16_t);
    uint16_t* units = mwinAllocate(&context->allocator, bytes, alignof(uint16_t));
    if (units != nullptr)
    {
        [string getCharacters:(unichar*)units range:NSMakeRange(0, length)];
        *bytesOut = bytes;
    }
    return units;
}

mwinOutcome mwinAppleTakeClipboardText(mwinContext* context, NSString* text)
{
    // Each unit is at least a byte of UTF-8: text past the limit is not
    // copied.
    if (text.length > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    size_t bytes = 0;
    uint16_t* units = Units(context, text, &bytes);
    if (units == nullptr && text.length > 0)
    {
        return mwin_outcomeFailed;
    }
    mwinOutcome outcome = mwinTakeClipboardUtf16(context, units, text.length);
    if (units != nullptr)
    {
        mwinRelease(&context->allocator, units, bytes, alignof(uint16_t));
    }
    return outcome;
}

void mwinAppleSetDroppedText(mwinContext* context, NSString* text)
{
    size_t bytes = 0;
    uint16_t* units =
        text.length <= context->limits.dropBytes ? Units(context, text, &bytes) : nullptr;
    if (units == nullptr && text.length > 0)
    {
        context->dropping.truncated = true;
        return;
    }
    mwinSetDroppedTextUtf16(context, units, text.length);
    if (units != nullptr)
    {
        mwinRelease(&context->allocator, units, bytes, alignof(uint16_t));
    }
}

bool mwinAppleImeText(NSString* string, uint16_t* units, char* bytes, size_t capacity,
                      size_t* lengthOut)
{
    *lengthOut = 0;
    NSUInteger count = string.length;
    if (count > capacity)
    {
        return false;
    }
    [string getCharacters:(unichar*)units range:NSMakeRange(0, count)];
    size_t length = 0;
    if (muniConvertUtf16ToUtf8(units, count, muni_convertReplace, bytes, capacity, &length)
            .status != muni_success)
    {
        return false;
    }
    *lengthOut = length;
    return true;
}
