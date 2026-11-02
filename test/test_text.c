// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fitting of compositions below the stream, at the text's very end,
// whose text sits alone in a heap allocation of its length so that
// AddressSanitizer sees a read past it; and the stream's own check of
// the text a backend posts, which backends repair first; and UTF-16 cut
// to the whole characters that fit, from units that end where their
// heap allocation does, more than the cut reads. The text storage itself
// is tested through the stream in test_input.c.

#include "core.h"
#include "preedit_fit.h"
#include "test_program.h"
#include "utf8.h"

#include <stdlib.h>
#include <string.h>

// Fits a composition of text held in an exact heap copy.
static mwinPreeditEvent Fit(const char* text, int32_t caret, uint32_t selectionStart,
                            uint32_t selectionEnd, mwinPreeditSegment* segments, uint32_t count)
{
    uint32_t length = (uint32_t)strlen(text);
    char* copy = malloc(length);
    mwinPreeditEvent preedit = {copy, length, caret, selectionStart, selectionEnd, segments, count};
    if (copy == nullptr)
    {
        return (mwinPreeditEvent){0};
    }
    memcpy(copy, text, length);
    mwinFitPreedit(&preedit, segments);
    free(copy);
    preedit.text = nullptr;
    return preedit;
}

static void TestFitAtTheEnds(void)
{
    // a, then ä in two bytes.
    mwinPreeditEvent fitted = Fit("a\xC3\xA4", 0, 0, 0, nullptr, 0);
    CHECK(fitted.length == 3 && fitted.caret == 0 && fitted.selectionStart == 0 &&
              fitted.selectionEnd == 0,
          "a caret at the start stays");
    fitted = Fit("a\xC3\xA4", 3, 1, 3, nullptr, 0);
    CHECK(fitted.caret == 3 && fitted.selectionStart == 1 && fitted.selectionEnd == 3,
          "a caret and a selection at the very end stay");
    fitted = Fit("a\xC3\xA4", 2, 3, 2, nullptr, 0);
    CHECK(fitted.caret == 1 && fitted.selectionStart == 1 && fitted.selectionEnd == 3,
          "inside the character: the caret to its start, the selection over it, in order");
    mwinPreeditSegment segments[2] = {{2, 9, mwin_preeditTarget}, {3, 1, 0}};
    fitted = Fit("a\xC3\xA4", -5, 9, 9, segments, 2);
    CHECK(fitted.caret == -1 && fitted.selectionStart == 3 && fitted.selectionEnd == 3 &&
              fitted.segmentCount == 1 && segments[0].start == 1 && segments[0].length == 2 &&
              segments[0].style == mwin_preeditTarget,
          "past the end: offsets at the end, an empty segment dropped");
}

// A backend that posts text it did not repair loses the record; the
// stream hands out only well-formed text and readable segments.
static void BackendStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    uint32_t slot = window.index1 - 1;
    mwinEvent bad = {.kind = mwin_eventTextInput, .window = window};
    bad.data.text = (mwinTextEvent){"a\xFF", 2};
    mwinPost(context, slot, &bad);
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS + 1] = {0};
    mwinEvent many = {.kind = mwin_eventImePreedit, .window = window};
    many.data.preedit =
        (mwinPreeditEvent){"ab", 2, -1, 0, 0, segments, MWIN_MAX_PREEDIT_SEGMENTS + 1};
    mwinPost(context, slot, &many);
    mwinEvent good = {.kind = mwin_eventTextInput, .window = window};
    good.data.text = (mwinTextEvent){"ok", 2};
    mwinPost(context, slot, &good);
    Drain(program, context);
    CHECK(program->eventCount == 1 && program->events[0].kind == mwin_eventTextInput &&
              program->events[0].data.text.length == 2 &&
              memcmp(program->events[0].data.text.text, "ok", 2) == 0,
          "ill-formed text and too many segments are lost, good text arrives");
    program->done = true;
}

// The UTF-8 of ASCII text and the units after it, cut to capacity.
static size_t Cut(const char* ascii, const uint16_t* tail, size_t tailCount, size_t capacity,
                  char* out)
{
    size_t length = strlen(ascii);
    uint16_t units[80];
    for (size_t i = 0; i < length; i++)
    {
        units[i] = (uint16_t)(unsigned char)ascii[i];
    }
    if (tailCount > 0)
    {
        memcpy(units + length, tail, tailCount * sizeof(uint16_t));
    }
    return mwinCutUtf16(units, length + tailCount, out, capacity);
}

static void TestCutUtf16(void)
{
    char out[72];
    static const char sixty[] = "0123456789012345678901234567890123456789012345678901234567890";
    CHECK(Cut("Xbox Wireless Controller", nullptr, 0, 64, out) == 24 &&
              memcmp(out, "Xbox Wireless Controller", 24) == 0,
          "a name that fits kept whole");
    // 61 bytes, then é (2), then U+20AC (3): 63, the euro left out.
    static const uint16_t accents[] = {0xE9, 0x20AC};
    size_t length = Cut(sixty, accents, 2, 64, out);
    CHECK(length == 63 && memcmp(out + 61, "\xC3\xA9", 2) == 0, "cut before a character");
    // 61 bytes, then U+1F3AE as a pair: four bytes do not fit in three.
    static const uint16_t pair[] = {0xD83C, 0xDFAE};
    CHECK(Cut(sixty, pair, 2, 64, out) == 61, "a pair never split");
    CHECK(Cut(sixty, pair, 2, 65, out) == 65 && memcmp(out + 61, "\xF0\x9F\x8E\xAE", 4) == 0,
          "a pair that fits kept");
    // A lone high surrogate at the end, a lone low one inside.
    static const uint16_t lone[] = {0xDC00, 'b', 0xD800};
    CHECK(Cut("a", lone, 3, 64, out) == 8 && memcmp(out,
                                                    "a\xEF\xBF\xBD"
                                                    "b\xEF\xBF\xBD",
                                                    8) == 0,
          "an unpaired surrogate as U+FFFD");
    CHECK(mwinCutUtf16(nullptr, 0, out, 64) == 0 && Cut("abc", nullptr, 0, 0, nullptr) == 0,
          "nothing for no units or no room");
    // Units said to be a million, held up to 66: a cut of 64 reads no
    // more than capacity + 2, where AddressSanitizer would see it.
    uint16_t* many = malloc(66 * sizeof(uint16_t));
    if (many != nullptr)
    {
        for (size_t i = 0; i < 66; i++)
        {
            many[i] = 'x';
        }
        CHECK(mwinCutUtf16(many, 1u << 20, out, 64) == 64, "no more read than the cut needs");
        free(many);
    }
}

static void TestBackendText(void)
{
    Program program = {.step = BackendStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

int main(void)
{
    TestFitAtTheEnds();
    TestCutUtf16();
    TestBackendText();
    return s_failures == 0 ? 0 : 1;
}
