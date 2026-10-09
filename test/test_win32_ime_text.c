// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's shaping of a composition, without an input method:
// the UTF-8 bytes before each unit at the edges of one, two, three and
// four bytes, a pair at the surrogates' last values, a lone surrogate at
// the end; the clauses from the attributes (input, target, converted),
// the target's span as the selection, units past the attributes plain,
// the units' count kept to; the caret at the start, inside and none.

#include "test_harness.h"
#include "win32_ime.h"

#include <imm.h>

static void TestBytes(void)
{
    static const WCHAR units[] = {0x7F, 0x80, 0x7FF, 0x800, 0xDBFF, 0xDFFF, 0xFFFF, 0xD800};
    static const uint32_t before[] = {0, 1, 3, 5, 8, 12, 12, 15, 18};
    bool all = true;
    for (uint32_t i = 0; i <= 8; i++)
    {
        // Index 5 falls inside the pair, which counts whole.
        all = all && (i == 5 || mwinWin32Utf8Before(units, 8, i) == before[i]);
    }
    CHECK(all, "one, two, three and four bytes at their edges, a lone surrogate last");
    CHECK(mwinWin32Utf8Before(units, 8, 99) == 18 && mwinWin32Utf8Before(units, 2, 8) == 3,
          "the units' count kept to");
}

static bool Is(const mwinPreeditSegment* segment, uint32_t start, uint32_t length,
               mwinPreeditStyle style)
{
    return segment->start == start && segment->length == length && segment->style == style;
}

static void TestClauses(void)
{
    // "aé€x": a 1, é 2, € 3, x 1 bytes; one unit past the count.
    static const WCHAR units[] = {L'a', 0xE9, 0x20AC, L'x', L'y'};
    static const BYTE clauses[] = {ATTR_INPUT, ATTR_TARGET_CONVERTED, ATTR_TARGET_NOTCONVERTED,
                                   ATTR_CONVERTED, ATTR_TARGET_CONVERTED};
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
    mwinPreeditEvent preedit = {0};
    mwinWin32ShapePreedit(units, 4, clauses, 5, 2, segments, &preedit);
    CHECK(preedit.segmentCount == 3 && preedit.segments == segments &&
              Is(&segments[0], 0, 1, mwin_preeditUnderline) &&
              Is(&segments[1], 1, 5, mwin_preeditTarget) &&
              Is(&segments[2], 6, 1, mwin_preeditConverted),
          "the clauses from the attributes, to the units' count");
    CHECK(preedit.caret == 3 && preedit.selectionStart == 1 && preedit.selectionEnd == 6,
          "the caret in bytes, the target's span the selection");
    // Two attributes for four units: the rest unattributed.
    mwinWin32ShapePreedit(units, 4, clauses, 2, 0, segments, &preedit);
    CHECK(preedit.segmentCount == 3 && Is(&segments[1], 1, 2, mwin_preeditTarget) &&
              Is(&segments[2], 3, 4, mwin_preeditPlain),
          "units past the attributes plain");
    // No target, the caret at the start: the selection empty there.
    static const BYTE inputs[] = {ATTR_INPUT, ATTR_INPUT, ATTR_INPUT, ATTR_INPUT};
    mwinWin32ShapePreedit(units, 4, inputs, 4, 0, segments, &preedit);
    CHECK(preedit.segmentCount == 1 && Is(&segments[0], 0, 7, mwin_preeditUnderline) &&
              preedit.caret == 0 && preedit.selectionStart == 0 && preedit.selectionEnd == 0,
          "one clause, the caret at the start and the selection empty there");
    mwinWin32ShapePreedit(units, 4, inputs, 0, -1, segments, &preedit);
    CHECK(preedit.caret == -1 && preedit.selectionStart == 0 && preedit.selectionEnd == 0 &&
              preedit.segmentCount == 1 && Is(&segments[0], 0, 7, mwin_preeditPlain),
          "no caret, no attributes");
}

int main(void)
{
    TestBytes();
    TestClauses();
    return s_failures == 0 ? 0 : 1;
}
