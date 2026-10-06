// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the fitting of a composition's offsets (preedit_fit.c), as an
// input method hands them: a caret, a selection and segments from the
// input's first bytes, the text from the rest, made well-formed first
// as every backend does. Fitted, every offset must lie on a character
// boundary within the text, the selection in order, every segment
// non-empty and inside the text, and fitting again must change nothing.

#include "preedit_fit.h"
#include "utf8.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_BYTES 1024

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

static bool OnBoundary(const char* text, uint32_t length, uint32_t at)
{
    return at <= length && (at == length || ((unsigned char)text[at] & 0xC0u) != 0x80u);
}

static void Check(const mwinPreeditEvent* preedit)
{
    const char* text = preedit->text;
    uint32_t length = preedit->length;
    Expect(preedit->caret >= -1 && preedit->caret <= (int64_t)length);
    Expect(preedit->caret < 0 || OnBoundary(text, length, (uint32_t)preedit->caret));
    Expect(preedit->selectionStart <= preedit->selectionEnd);
    Expect(OnBoundary(text, length, preedit->selectionStart) &&
           OnBoundary(text, length, preedit->selectionEnd));
    Expect(preedit->segmentCount <= MWIN_MAX_PREEDIT_SEGMENTS);
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        const mwinPreeditSegment* segment = &preedit->segments[i];
        Expect(segment->length > 0 && segment->start < length &&
               length - segment->start >= segment->length);
        Expect(OnBoundary(text, length, segment->start) &&
               OnBoundary(text, length, segment->start + segment->length));
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    int32_t caret = 0;
    uint32_t selection[2] = {0};
    uint8_t count = 0;
    Take(&data, &size, &caret, sizeof(caret));
    Take(&data, &size, selection, sizeof(selection));
    Take(&data, &size, &count, sizeof(count));
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
    count = (uint8_t)(count % (MWIN_MAX_PREEDIT_SEGMENTS + 1));
    for (uint8_t i = 0; i < count; i++)
    {
        uint32_t values[2] = {0};
        Take(&data, &size, values, sizeof(values));
        segments[i] = (mwinPreeditSegment){values[0], values[1], mwin_preeditUnderline};
    }
    // The text, made well-formed as backends make it, within the bound.
    static char text[TEXT_BYTES * 3];
    size_t raw = size < TEXT_BYTES ? size : TEXT_BYTES;
    size_t length = mwinRepairUtf8((const char*)data, raw, text);
    mwinPreeditEvent preedit = {text,         (uint32_t)length, caret, selection[0],
                                selection[1], segments,         count};
    mwinFitPreedit(&preedit, segments);
    Check(&preedit);
    mwinPreeditEvent again = preedit;
    mwinPreeditSegment copies[MWIN_MAX_PREEDIT_SEGMENTS];
    memcpy(copies, segments, sizeof(mwinPreeditSegment) * preedit.segmentCount);
    mwinFitPreedit(&again, copies);
    Expect(again.caret == preedit.caret && again.selectionStart == preedit.selectionStart &&
           again.selectionEnd == preedit.selectionEnd &&
           again.segmentCount == preedit.segmentCount &&
           memcmp(copies, segments, sizeof(mwinPreeditSegment) * preedit.segmentCount) == 0);
    return 0;
}
