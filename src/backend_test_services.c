// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The services the test platform plays: its clipboard, and drops.

#include "allocator.h"
#include "backend_test.h"

#include "maul-window/test.h"

#include <string.h>

void mwinTestReleaseClipboard(const mwinContext* context, mwinTestPlatform* platform)
{
    if (platform->clipboard != nullptr)
    {
        mwinRelease(&context->allocator, platform->clipboard, platform->clipboardBytes,
                    alignof(uint16_t));
    }
    platform->clipboard = nullptr;
    platform->clipboardBytes = 0;
}

// Puts bytes on the platform's clipboard; false when there is no room.
static bool SetClipboard(const mwinContext* context, const void* data, size_t bytes, bool utf16)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    void* copy = bytes > 0 ? mwinAllocate(&context->allocator, bytes, alignof(uint16_t)) : nullptr;
    if (bytes > 0 && copy == nullptr)
    {
        return false;
    }
    if (bytes > 0)
    {
        memcpy(copy, data, bytes);
    }
    mwinTestReleaseClipboard(context, platform);
    platform->clipboard = copy;
    platform->clipboardBytes = bytes;
    platform->utf16 = utf16;
    return true;
}

mwinOutcome mwinTestUseClipboard(mwinContext* context, mwinRequestKind kind)
{
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (kind == mwin_requestClipboardWrite)
    {
        return SetClipboard(context, context->clipboardOffer, context->clipboardOfferLength, false)
                   ? mwin_outcomeDone
                   : mwin_outcomeFailed;
    }
    return platform->utf16
               ? mwinTakeClipboardUtf16(context, platform->clipboard, platform->clipboardBytes / 2)
               : mwinTakeClipboardText(context, platform->clipboard, platform->clipboardBytes);
}

mwinResult mwinTestSetClipboard(mwinContext* context, const char* bytes, size_t length)
{
    if (context == nullptr || (bytes == nullptr && length != 0))
    {
        return mwin_errorInvalid;
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    return SetClipboard(context, bytes, length, false) ? mwin_success : mwin_errorCapacity;
}

mwinResult mwinTestSetClipboardUtf16(mwinContext* context, const uint16_t* units, size_t length)
{
    if (context == nullptr || (units == nullptr && length != 0))
    {
        return mwin_errorInvalid;
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    return SetClipboard(context, units, length * 2, true) ? mwin_success : mwin_errorCapacity;
}

mwinResult mwinTestGetClipboard(const mwinContext* context, char* buffer, size_t capacity,
                                size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwin_errorInvalid;
    }
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    size_t length = platform->clipboardBytes;
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, platform->clipboard, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinTestDrop(mwinContext* context, mwinWindowId window, mwinPosition position,
                        const char* files, size_t filesLength, const char* text, size_t textLength)
{
    if (context == nullptr || (files == nullptr && filesLength != 0) ||
        (filesLength > 0 && files[filesLength - 1] != '\0'))
    {
        return mwin_errorInvalid;
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (mwinFindWindow(context, window) == nullptr)
    {
        return mwin_errorStale;
    }
    if (platform->dropWaiting)
    {
        return mwin_errorState;
    }
    mwinEvent report = {.type = mwin_eventDropped, .window = window, .timeNs = platform->timeNs};
    report.data.drop.position = position;
    mwinResult status = mwinTestQueueReport(platform, &report);
    if (status != mwin_success)
    {
        return status;
    }
    mwinBeginDrop(context);
    for (size_t at = 0; at < filesLength; at += strlen(files + at) + 1)
    {
        mwinAddDroppedFile(context, files + at, strlen(files + at));
    }
    if (text != nullptr)
    {
        mwinSetDroppedText(context, text, textLength);
    }
    platform->dropWaiting = true;
    return mwin_success;
}
