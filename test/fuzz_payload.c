// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the payloads other programs hand the backends, as the backends
// give them to the core: text/uri-list drops, dropped text and files in
// UTF-8 and UTF-16, clipboard text in both, and zenity's output. Each
// input runs on a context of the test backend; its first byte chooses
// the payload. Whatever is kept must be UTF-8 within its limit, and a
// file list must hold its count of non-empty paths and nothing else.

#include "core.h"
#include "dialog.h"
#include "uri_list.h"
#ifdef MAUL_WINDOW_ZENITY
#include "linux_zenity.h"
#endif

#include "maul-unicode/encoding.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t* s_data;
static size_t s_size;

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static bool IsUtf8(const char* text, size_t length)
{
    return muniValidateUtf8(text, length).status == muni_success;
}

// The paths, each ended by a NUL, are the list's bytes, within its
// bounds, which count the NULs.
static void CheckList(const mwinFileList* list, uint32_t count, uint32_t bytes)
{
    size_t at = 0;
    for (uint32_t i = 0; i < list->count; i++)
    {
        const char* path = list->bytes + at;
        const char* end = memchr(path, '\0', list->length - at);
        size_t length = end != nullptr ? (size_t)(end - path) : list->length - at;
        Expect(length > 0 && at + length < list->length && IsUtf8(path, length));
        at += length + 1;
    }
    Expect(at == list->length && list->length <= bytes && list->count <= count);
}

static void Feed(mwinContext* context, char* bytes, size_t length, int kind)
{
    const mwinLimits* limits = &context->limits;
    uint16_t* units = (uint16_t*)bytes;
    switch (kind)
    {
    case 0:
        mwinBeginDrop(context);
        mwinGatherUriList(context, bytes, length);
        break;
    case 1:
        mwinBeginDrop(context);
        mwinSetDroppedText(context, bytes, length);
        break;
    case 2:
        mwinBeginDrop(context);
        mwinSetDroppedTextUtf16(context, units, length / 2);
        break;
    case 3:
        mwinBeginDrop(context);
        mwinAddDroppedFileUtf16(context, units, length / 2);
        mwinAddDroppedFile(context, bytes, length);
        break;
    case 4:
    case 5:
    {
        mwinOutcome outcome = kind == 4 ? mwinTakeClipboardText(context, bytes, length)
                                        : mwinTakeClipboardUtf16(context, units, length / 2);
        Expect(outcome != mwin_outcomeDone ||
               (context->clipboardFoundLength <= limits->clipboardBytes &&
                IsUtf8(context->clipboardFound, context->clipboardFoundLength)));
        return;
    }
    default:
#ifdef MAUL_WINDOW_ZENITY
        mwinZenityGather(context, bytes, length);
        CheckList(&context->dialogGathering, limits->dialogFiles, limits->dialogBytes);
#endif
        return;
    }
    const mwinDropPayload* drop = &context->dropping;
    CheckList(&drop->files, limits->droppedFiles, limits->dropBytes);
    Expect(drop->textLength <= limits->dropBytes && IsUtf8(drop->text, drop->textLength));
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    size_t length = s_size - 1;
    // The bytes as their own block, aligned for UTF-16, which the
    // decoders change in place.
    char* bytes = malloc(length + 2);
    Expect(bytes != nullptr);
    memcpy(bytes, s_data + 1, length);
    Feed(context, bytes, length, s_data[0] % 7);
    free(bytes);
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_frameStop;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0)
    {
        return 0;
    }
    s_data = data;
    s_size = size;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.init = Init;
    def.frame = Frame;
    Expect(mwinRun(&def) == mwin_success);
    return 0;
}
