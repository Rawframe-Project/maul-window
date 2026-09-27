// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drops: their files and text as backends gather them, and the copies
// the program takes.

#include "maul-window/drop.h"

#include "allocator.h"
#include "core.h"
#include "utf8.h"

#include "maul-unicode/encoding.h"

#include <string.h>

static void ReleasePayload(const mwinContext* context, mwinDropPayload* payload)
{
    if (payload->files != nullptr)
    {
        mwinRelease(&context->allocator, payload->files, payload->filesCapacity, 1);
    }
    if (payload->text != nullptr)
    {
        mwinRelease(&context->allocator, payload->text, payload->textLength, 1);
    }
    *payload = (mwinDropPayload){0};
}

void mwinBeginDrop(mwinContext* context)
{
    ReleasePayload(context, &context->dropping);
}

// Room for a path of a length and its NUL: where it goes, or NULL, the
// drop marked truncated, past the limits or the allocator's room.
static char* PathRoom(mwinContext* context, size_t length)
{
    mwinDropPayload* drop = &context->dropping;
    const mwinLimits* limits = &context->limits;
    size_t needed = (size_t)drop->filesLength + length + 1;
    if (drop->fileCount >= limits->droppedFiles || needed > limits->dropBytes)
    {
        drop->truncated = true;
        return nullptr;
    }
    if (needed > drop->filesCapacity)
    {
        size_t capacity = (size_t)drop->filesCapacity * 2;
        capacity = capacity < needed ? needed : capacity;
        capacity = capacity < limits->dropBytes ? capacity : limits->dropBytes;
        char* grown = mwinAllocate(&context->allocator, capacity, 1);
        if (grown == nullptr)
        {
            drop->truncated = true;
            return nullptr;
        }
        if (drop->files != nullptr)
        {
            memcpy(grown, drop->files, drop->filesLength);
            mwinRelease(&context->allocator, drop->files, drop->filesCapacity, 1);
        }
        drop->files = grown;
        drop->filesCapacity = (uint32_t)capacity;
    }
    return drop->files + drop->filesLength;
}

// Counts a path written where PathRoom said, and ends it.
static void CountPath(mwinDropPayload* drop, size_t length)
{
    drop->files[drop->filesLength + length] = '\0';
    drop->filesLength += (uint32_t)length + 1;
    drop->fileCount += 1;
}

void mwinAddDroppedFile(mwinContext* context, const char* path, size_t length)
{
    // A path with a NUL or that is not UTF-8 names no file for the
    // program.
    if (length == 0 || memchr(path, '\0', length) != nullptr ||
        muniValidateUtf8(path, length).status != muni_success)
    {
        context->dropping.truncated = true;
        return;
    }
    char* room = PathRoom(context, length);
    if (room != nullptr)
    {
        memcpy(room, path, length);
        CountPath(&context->dropping, length);
    }
}

void mwinAddDroppedFileUtf16(mwinContext* context, const uint16_t* path, size_t length)
{
    size_t needed = 0;
    muniTextResult converted =
        muniConvertUtf16ToUtf8(path, length, nullptr, 0, muni_convertStrict, &needed);
    bool nul = false;
    for (size_t i = 0; i < length && !nul; i++)
    {
        nul = path[i] == 0;
    }
    if (length == 0 || nul || converted.status == muni_errorUtf16Surrogate)
    {
        context->dropping.truncated = true;
        return;
    }
    char* room = PathRoom(context, needed);
    if (room != nullptr)
    {
        (void)muniConvertUtf16ToUtf8(path, length, room, needed, muni_convertStrict, &needed);
        CountPath(&context->dropping, needed);
    }
}

// Room for the text of a length, the drop marked truncated when it has
// none.
static char* TextRoom(mwinContext* context, size_t length)
{
    mwinDropPayload* drop = &context->dropping;
    if (length > context->limits.dropBytes)
    {
        drop->truncated = true;
        return nullptr;
    }
    char* text = length > 0 ? mwinAllocate(&context->allocator, length, 1) : nullptr;
    drop->truncated = drop->truncated || (length > 0 && text == nullptr);
    if (text != nullptr)
    {
        if (drop->text != nullptr)
        {
            mwinRelease(&context->allocator, drop->text, drop->textLength, 1);
        }
        drop->text = text;
        drop->textLength = (uint32_t)length;
    }
    return text;
}

void mwinSetDroppedText(mwinContext* context, const char* bytes, size_t length)
{
    // Repairing never shortens the text.
    if (length > context->limits.dropBytes)
    {
        context->dropping.truncated = true;
        return;
    }
    char* text = TextRoom(context, mwinRepairUtf8(bytes, length, nullptr));
    if (text != nullptr)
    {
        (void)mwinRepairUtf8(bytes, length, text);
    }
}

void mwinSetDroppedTextUtf16(mwinContext* context, const uint16_t* units, size_t length)
{
    size_t needed = 0;
    (void)muniConvertUtf16ToUtf8(units, length, nullptr, 0, muni_convertReplace, &needed);
    char* text = TextRoom(context, needed);
    if (text != nullptr)
    {
        (void)muniConvertUtf16ToUtf8(units, length, text, needed, muni_convertReplace, &needed);
    }
}

void mwinFinishDrop(mwinContext* context, uint32_t slot, mwinPosition position, uint64_t timeNs)
{
    ReleasePayload(context, &context->dropped);
    context->dropped = context->dropping;
    context->dropping = (mwinDropPayload){0};
    context->dropNumber += 1;
    const mwinDropPayload* drop = &context->dropped;
    mwinEvent event = {.type = mwin_eventDropped, .timeNs = timeNs};
    event.data.drop = (mwinDropEvent){position, context->dropNumber, drop->fileCount,
                                      drop->textLength, drop->truncated};
    mwinPost(context, slot, &event);
}

void mwinReleaseDrops(mwinContext* context)
{
    ReleasePayload(context, &context->dropping);
    ReleasePayload(context, &context->dropped);
}

// Copies bytes of the delivered drop out.
static mwinResult CopyOut(const mwinContext* context, uint32_t drop, const char* bytes,
                          size_t length, char* buffer, size_t capacity, size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwin_errorInvalid;
    }
    if (drop == 0 || drop != context->dropNumber)
    {
        return mwin_errorStale;
    }
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, bytes, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinGetDroppedFiles(const mwinContext* context, uint32_t drop, char* buffer,
                               size_t capacity, size_t* lengthOut)
{
    return CopyOut(context, drop, context != nullptr ? context->dropped.files : nullptr,
                   context != nullptr ? context->dropped.filesLength : 0, buffer, capacity,
                   lengthOut);
}

mwinResult mwinGetDroppedText(const mwinContext* context, uint32_t drop, char* buffer,
                              size_t capacity, size_t* lengthOut)
{
    return CopyOut(context, drop, context != nullptr ? context->dropped.text : nullptr,
                   context != nullptr ? context->dropped.textLength : 0, buffer, capacity,
                   lengthOut);
}
