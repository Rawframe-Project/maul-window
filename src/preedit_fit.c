// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fitting a composition's offsets to its text, and dropping control
// characters.

#include "preedit_fit.h"

// Whether a byte continues a character of UTF-8.
static bool Continues(const char* text, uint32_t at)
{
    return ((unsigned char)text[at] & 0xC0u) == 0x80u;
}

// An offset within the text, moved back to the start of the character
// it falls in.
static uint32_t Floor(const char* text, uint32_t length, uint32_t offset)
{
    uint32_t at = offset < length ? offset : length;
    while (at > 0 && at < length && Continues(text, at))
    {
        at--;
    }
    return at;
}

// An offset within the text, moved on past the character it falls in.
static uint32_t Ceiling(const char* text, uint32_t length, uint32_t offset)
{
    uint32_t at = offset < length ? offset : length;
    while (at < length && Continues(text, at))
    {
        at++;
    }
    return at;
}

// The bytes of a control character at a byte of well-formed UTF-8: one
// for C0 and DEL, two for C1 (0xC2 then 0x80 to 0x9F), else 0.
static uint32_t ControlAt(const char* text, uint32_t length, uint32_t at)
{
    unsigned char byte = (unsigned char)text[at];
    if (byte < 0x20u || byte == 0x7Fu)
    {
        return 1;
    }
    return byte == 0xC2u && at + 1 < length && (unsigned char)text[at + 1] < 0xA0u ? 2 : 0;
}

// Where an offset of the text falls once its control characters go.
static uint32_t Kept(const char* text, uint32_t length, uint32_t offset)
{
    uint32_t gone = 0;
    for (uint32_t at = 0; at < length && at < offset;)
    {
        uint32_t control = ControlAt(text, length, at);
        if (control == 0)
        {
            at++;
            continue;
        }
        gone += offset - at < control ? offset - at : control;
        at += control;
    }
    return offset - gone;
}

uint32_t mwinKeptTextBytes(const char* text, uint32_t length)
{
    uint32_t kept = length;
    for (uint32_t at = 0; at < length;)
    {
        uint32_t control = ControlAt(text, length, at);
        kept -= control;
        at += control > 0 ? control : 1;
    }
    return kept;
}

void mwinCopyKeptText(char* out, const char* text, uint32_t length, mwinPreeditEvent* preedit,
                      mwinPreeditSegment* segments)
{
    uint32_t kept = 0;
    for (uint32_t at = 0; at < length;)
    {
        uint32_t control = ControlAt(text, length, at);
        if (control == 0)
        {
            out[kept++] = text[at];
        }
        at += control > 0 ? control : 1;
    }
    if (preedit == nullptr || kept == length)
    {
        return;
    }
    if (preedit->caret > 0)
    {
        preedit->caret = (int32_t)Kept(text, length, (uint32_t)preedit->caret);
    }
    preedit->selectionStart = Kept(text, length, preedit->selectionStart);
    preedit->selectionEnd = Kept(text, length, preedit->selectionEnd);
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        mwinPreeditSegment* segment = &segments[i];
        uint32_t past = segment->length < UINT32_MAX - segment->start
                            ? segment->start + segment->length
                            : UINT32_MAX;
        uint32_t from = Kept(text, length, segment->start);
        segment->length = Kept(text, length, past) - from;
        segment->start = from;
    }
}

void mwinFitPreedit(mwinPreeditEvent* preedit, mwinPreeditSegment* segments)
{
    const char* text = preedit->text;
    uint32_t length = preedit->length;
    if (preedit->caret >= 0)
    {
        preedit->caret = (int32_t)Floor(text, length, (uint32_t)preedit->caret);
    }
    else
    {
        preedit->caret = -1;
    }
    uint32_t start = preedit->selectionStart;
    uint32_t end = preedit->selectionEnd;
    preedit->selectionStart = Floor(text, length, start < end ? start : end);
    preedit->selectionEnd = Ceiling(text, length, start < end ? end : start);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        mwinPreeditSegment segment = segments[i];
        uint32_t from = Floor(text, length, segment.start);
        uint32_t past = segment.start < length && length - segment.start > segment.length
                            ? segment.start + segment.length
                            : length;
        past = Ceiling(text, length, past);
        if (past > from)
        {
            segments[kept++] = (mwinPreeditSegment){from, past - from, segment.style};
        }
    }
    preedit->segmentCount = kept;
    preedit->segments = segments;
}
