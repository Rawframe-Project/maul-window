// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A window's text storage and the fitting of compositions, below the
// stream: reservations that fit exactly before the head or the end,
// reclaiming after the ring started over, and offsets fitted at the
// text's very end, whose text sits alone in a heap allocation of its
// length so that AddressSanitizer sees a read past it; and the stream's
// own check of the text a backend posts, which backends repair first.

#include "core.h"
#include "preedit_fit.h"
#include "test_program.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

enum
{
    Capacity = 16,
};

static mwinTextRing Ring(char* bytes)
{
    return (mwinTextRing){.bytes = bytes, .capacity = Capacity};
}

static void TestExactFits(void)
{
    char bytes[Capacity];
    mwinTextRing ring = Ring(bytes);
    CHECK(mwinReserveText(&ring, 10, 1) == bytes && mwinReserveText(&ring, 6, 1) == bytes + 10,
          "a reservation that fills the end exactly");
    ring = Ring(bytes);
    char* a = mwinReserveText(&ring, 4, 1);
    char* b = mwinReserveText(&ring, 4, 1);
    CHECK(a == bytes && b == bytes + 4 && mwinReserveText(&ring, 8, 1) == bytes + 8,
          "three reservations fill the ring");
    mwinDrainText(&ring, a, a + 4);
    mwinDrainText(&ring, b, b + 4);
    mwinReclaimText(&ring);
    CHECK(mwinReserveText(&ring, 4, 1) == bytes, "the next starts over at the beginning");
    CHECK(mwinReserveText(&ring, 4, 1) == bytes + 4 && ring.busy == Capacity,
          "and one more fills the room before the head exactly");
    CHECK(mwinReserveText(&ring, 1, 1) == nullptr, "a full ring takes nothing");
}

static void TestReclaimAfterStartingOver(void)
{
    char bytes[Capacity];
    mwinTextRing ring = Ring(bytes);
    char* a = mwinReserveText(&ring, 6, 1);
    mwinDrainText(&ring, a, a + 6);
    mwinReclaimText(&ring);
    CHECK(ring.busy == 0 && ring.head == 6, "everything reclaimed");
    // Empty, the ring starts over at its beginning.
    char* b = mwinReserveText(&ring, 6, 1);
    CHECK(b == bytes, "an empty ring starts over");
    mwinReclaimText(&ring);
    CHECK(ring.head == 0 && ring.busy == 6, "nothing drained, nothing reclaimed");
    mwinDrainText(&ring, b, b + 6);
    mwinReclaimText(&ring);
    CHECK(ring.busy == 0 && ring.head == 6, "the second reservation reclaimed alone");
}

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
    mwinEvent bad = {.type = mwin_eventTextInput, .window = window};
    bad.data.text = (mwinTextEvent){"a\xFF", 2};
    mwinPost(context, slot, &bad);
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS + 1] = {0};
    mwinEvent many = {.type = mwin_eventImePreedit, .window = window};
    many.data.preedit =
        (mwinPreeditEvent){"ab", 2, -1, 0, 0, segments, MWIN_MAX_PREEDIT_SEGMENTS + 1};
    mwinPost(context, slot, &many);
    mwinEvent good = {.type = mwin_eventTextInput, .window = window};
    good.data.text = (mwinTextEvent){"ok", 2};
    mwinPost(context, slot, &good);
    Drain(program, context);
    CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventTextInput &&
              program->events[0].data.text.length == 2 &&
              memcmp(program->events[0].data.text.text, "ok", 2) == 0,
          "ill-formed text and too many segments are lost, good text arrives");
    program->done = true;
}

static void TestBackendText(void)
{
    Program program = {.step = BackendStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

int main(void)
{
    TestExactFits();
    TestReclaimAfterStartingOver();
    TestFitAtTheEnds();
    TestBackendText();
    return s_failures == 0 ? 0 : 1;
}
