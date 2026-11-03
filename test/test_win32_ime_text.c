// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's shaping of a composition, without an input method:
// the clauses from the attributes (input, target, converted), the
// target's span as the selection, units past the attributes plain, the
// units' count kept to; the caret at the start, inside and none; a
// caret inside a surrogate pair before the pair, as on every platform.

#include "test_harness.h"
#include "win32_ime.h"

#include <imm.h>

static bool Is(const mwinPreeditSegment* segment, uint32_t start, uint32_t length,
               mwinPreeditStyle style)
{
    return segment->start == start && segment->length == length && segment->style == style;
}

static void TestClauses(void)
{
    // "aé€x": a 1, é 2, € 3, x 1 bytes; one attribute past the count.
    static const BYTE clauses[] = {ATTR_INPUT, ATTR_TARGET_CONVERTED, ATTR_TARGET_NOTCONVERTED,
                                   ATTR_CONVERTED, ATTR_TARGET_CONVERTED};
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
    mwinPreeditEvent preedit = {.text = "a\xC3\xA9\xE2\x82\xACx", .length = 7};
    mwinWin32ShapePreedit(4, clauses, 5, 2, segments, &preedit);
    CHECK(preedit.segmentCount == 3 && preedit.segments == segments &&
              Is(&segments[0], 0, 1, mwin_preeditUnderline) &&
              Is(&segments[1], 1, 5, mwin_preeditTarget) &&
              Is(&segments[2], 6, 1, mwin_preeditConverted),
          "the clauses from the attributes, to the units' count");
    CHECK(preedit.caret == 3 && preedit.selectionStart == 1 && preedit.selectionEnd == 6,
          "the caret in bytes, the target's span the selection");
    // Two attributes for four units: the rest unattributed.
    mwinWin32ShapePreedit(4, clauses, 2, 0, segments, &preedit);
    CHECK(preedit.segmentCount == 3 && Is(&segments[1], 1, 2, mwin_preeditTarget) &&
              Is(&segments[2], 3, 4, mwin_preeditPlain),
          "units past the attributes plain");
    // No target, the caret at the start: the selection empty there.
    static const BYTE inputs[] = {ATTR_INPUT, ATTR_INPUT, ATTR_INPUT, ATTR_INPUT};
    mwinWin32ShapePreedit(4, inputs, 4, 0, segments, &preedit);
    CHECK(preedit.segmentCount == 1 && Is(&segments[0], 0, 7, mwin_preeditUnderline) &&
              preedit.caret == 0 && preedit.selectionStart == 0 && preedit.selectionEnd == 0,
          "one clause, the caret at the start and the selection empty there");
    mwinWin32ShapePreedit(4, inputs, 0, -1, segments, &preedit);
    CHECK(preedit.caret == -1 && preedit.selectionStart == 0 && preedit.selectionEnd == 0 &&
              preedit.segmentCount == 1 && Is(&segments[0], 0, 7, mwin_preeditPlain),
          "no caret, no attributes");
}

static void TestPair(void)
{
    // "a", U+1F3AE as a pair (four bytes), "b"; the pair the target.
    static const BYTE clauses[] = {ATTR_INPUT, ATTR_TARGET_CONVERTED, ATTR_TARGET_CONVERTED,
                                   ATTR_INPUT};
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
    mwinPreeditEvent preedit = {.text = "a\xF0\x9F\x8E\xAE"
                                        "b",
                                .length = 6};
    mwinWin32ShapePreedit(4, clauses, 4, 2, segments, &preedit);
    CHECK(preedit.caret == 1, "a caret inside the pair before it");
    CHECK(preedit.segmentCount == 3 && Is(&segments[0], 0, 1, mwin_preeditUnderline) &&
              Is(&segments[1], 1, 4, mwin_preeditTarget) &&
              Is(&segments[2], 5, 1, mwin_preeditUnderline) && preedit.selectionStart == 1 &&
              preedit.selectionEnd == 5,
          "the pair's clause whole, its span the selection");
}

int main(void)
{
    TestClauses();
    TestPair();
    return s_failures == 0 ? 0 : 1;
}
