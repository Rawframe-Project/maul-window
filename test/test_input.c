// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The input contract against the test backend: keys and text in order,
// text that stays valid through its frame and wraps in its storage,
// discrete input that resets instead of vanishing, motion, deltas and
// wheel turns that merge when their storage is full, touches merged
// only with their own, refused records, the keyboard layout, cursor
// requests, and the chords the platform keeps.

#include "test_program.h"

#include <string.h>

static mwinEvent Key(mwinWindowId window, mwinEventType type, mwinKeyCode code, mwinKey key)
{
    mwinEvent event = {.type = type, .window = window};
    event.data.key.code = code;
    event.data.key.key = key;
    return event;
}

static mwinEvent Text(mwinWindowId window, const char* text)
{
    mwinEvent event = {.type = mwin_eventTextInput, .window = window};
    event.data.text.text = text;
    event.data.text.length = (uint32_t)strlen(text);
    return event;
}

static bool TextIs(const mwinEvent* event, const char* text)
{
    return event->type == mwin_eventTextInput && event->data.text.length == strlen(text) &&
           memcmp(event->data.text.text, text, strlen(text)) == 0;
}

static void KeyStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        mwinEvent events[3] = {Key(window, mwin_eventKeyDown, mwin_codeKeyA, 'a'),
                               Text(window, "\xC3\xA4"),
                               Key(window, mwin_eventKeyUp, mwin_codeKeyA, 'a')};
        for (int i = 0; i < 3; i++)
        {
            CHECK(mwinTestPost(context, &events[i]) == mwin_success, "a key and its text");
        }
        return;
    }
    static const mwinEventType expected[] = {mwin_eventKeyDown, mwin_eventTextInput,
                                             mwin_eventKeyUp};
    CHECK(Types(program, expected, 3) && program->events[0].data.key.code == mwin_codeKeyA &&
              program->events[0].data.key.key == 'a' && TextIs(&program->events[1], "\xC3\xA4"),
          "keys and text in order");
    program->done = true;
}

static void TestKeysAndText(void)
{
    Program program = {.step = KeyStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

// Takes one record of the stream.
static mwinEvent Next(mwinContext* context)
{
    mwinEvent event = {0};
    CHECK(mwinNextEvent(context, &event) == mwin_success, "a record waits");
    return event;
}

// 16 bytes of text storage: A and B fill 12; draining A frees its 6 at
// the next pump, and C wraps to the start past the 4 bytes left at the
// end; then three texts with nothing drained do not fit.
static void WrapStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    mwinEvent a = Text(window, "aaaaaa");
    mwinEvent b = Text(window, "bbbbbb");
    mwinEvent c = Text(window, "cccccc");
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        return;
    case 1:
        Drain(program, context);
        CHECK(mwinTestPost(context, &a) == mwin_success &&
                  mwinTestPost(context, &b) == mwin_success,
              "A and B");
        return;
    case 2:
    {
        mwinEvent first = Next(context);
        CHECK(TextIs(&first, "aaaaaa"), "A, while B waits");
        CHECK(mwinTestPost(context, &c) == mwin_success, "C");
        return;
    }
    case 3:
    {
        mwinEvent second = Next(context);
        mwinEvent third = Next(context);
        CHECK(TextIs(&second, "bbbbbb") && TextIs(&third, "cccccc"),
              "B, then C whole after the wrap");
        for (int i = 0; i < 3; i++)
        {
            CHECK(mwinTestPost(context, &a) == mwin_success, "three more");
        }
        return;
    }
    default:
        Drain(program, context);
        static const mwinEventType expected[] = {mwin_eventTextInput, mwin_eventTextInput,
                                                 mwin_eventInputStateReset};
        CHECK(Types(program, expected, 3), "a text that does not fit is a reset");
        program->done = true;
    }
}

static void TestTextStorage(void)
{
    Program program = {.step = WrapStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.textBytesPerWindow = 16;
    CHECK(RunWith(&program, def) == mwin_success, "the program runs");
}

static void OverflowStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        for (int i = 0; i < 5; i++)
        {
            mwinEvent key = Key(window, mwin_eventKeyDown, (mwinKeyCode)(mwin_codeKeyA + i), 0);
            CHECK(mwinTestPost(context, &key) == mwin_success, "five keys");
        }
        for (int i = 1; i <= 6; i++)
        {
            mwinEvent move = {.type = mwin_eventCursorMoved, .window = window};
            move.data.pointer.position = (mwinPosition){(float)i, 0.0f};
            mwinEvent delta = {.type = mwin_eventRawPointerDelta, .window = window};
            delta.data.delta = (mwinDeltaEvent){1.0f, 2.0f};
            mwinEvent wheel = {.type = mwin_eventWheel, .window = window};
            wheel.data.wheel = (mwinWheelEvent){0.0f, 0.5f};
            CHECK(mwinTestPost(context, &move) == mwin_success &&
                      mwinTestPost(context, &delta) == mwin_success &&
                      mwinTestPost(context, &wheel) == mwin_success,
                  "six of each continuous kind");
        }
        return;
    }
    int keys = 0;
    int resets = 0;
    float deltaX = 0.0f;
    float wheelY = 0.0f;
    const mwinEvent* lastMove = nullptr;
    int moveSamples = 0;
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        keys += event->type == mwin_eventKeyDown;
        resets += event->type == mwin_eventInputStateReset;
        CHECK(event->type != mwin_eventInputStateReset || keys == 4, "the reset where the loss");
        deltaX += event->type == mwin_eventRawPointerDelta ? event->data.delta.x : 0.0f;
        wheelY += event->type == mwin_eventWheel ? event->data.wheel.y : 0.0f;
        lastMove = event->type == mwin_eventCursorMoved ? event : lastMove;
        moveSamples += event->type == mwin_eventCursorMoved ? event->samples : 0;
    }
    CHECK(keys == 4 && resets == 1, "the fifth key is lost to a reset");
    CHECK(deltaX == 6.0f && wheelY == 3.0f, "deltas and wheel turns add up when merged");
    CHECK(lastMove != nullptr && lastMove->data.pointer.position.x == 6.0f &&
              lastMove->samples == 3 && moveSamples == 6,
          "merged motion ends at the newest position and counts its samples");
    program->done = true;
}

static void TestOverflow(void)
{
    Program program = {.step = OverflowStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.inputPerWindow = 4;
    CHECK(RunWith(&program, def) == mwin_success, "the program runs");
}

// A pen's button is discrete input: when its storage is full it is lost
// to a reset like a key.
static void PenStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        for (int i = 0; i < 4; i++)
        {
            mwinEvent key = Key(window, mwin_eventKeyDown, mwin_codeKeyA, 'a');
            CHECK(mwinTestPost(context, &key) == mwin_success, "four keys fill the storage");
        }
        mwinEvent up = {.type = mwin_eventPenButtonUp, .window = window};
        CHECK(mwinTestPost(context, &up) == mwin_success, "then a pen's button");
        return;
    }
    static const mwinEventType expected[] = {mwin_eventKeyDown, mwin_eventKeyDown,
                                             mwin_eventKeyDown, mwin_eventKeyDown,
                                             mwin_eventInputStateReset};
    CHECK(Types(program, expected, 5), "the button is lost to a reset");
    program->done = true;
}

static void TestPenButtonOverflow(void)
{
    Program program = {.step = PenStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.inputPerWindow = 4;
    CHECK(RunWith(&program, def) == mwin_success, "the program runs");
}

// A notification that replaces a waiting one moves to the end; the
// records after the replaced one keep their places in the stream.
static void ReplaceStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        mwinEvent first = {.type = mwin_eventResized, .window = window};
        first.data.size = (mwinSize){10.0f, 10.0f};
        mwinEvent key = Key(window, mwin_eventKeyDown, mwin_codeKeyA, 'a');
        mwinEvent moved = {.type = mwin_eventMoved, .window = window};
        moved.data.position = (mwinPosition){5.0f, 5.0f};
        mwinEvent second = first;
        second.data.size = (mwinSize){20.0f, 20.0f};
        CHECK(mwinTestPost(context, &first) == mwin_success &&
                  mwinTestPost(context, &key) == mwin_success &&
                  mwinTestPost(context, &moved) == mwin_success &&
                  mwinTestPost(context, &second) == mwin_success,
              "a size, a key, a move and a new size");
        return;
    }
    static const mwinEventType expected[] = {mwin_eventKeyDown, mwin_eventMoved, mwin_eventResized};
    CHECK(Types(program, expected, 3) && program->events[2].data.size.width == 20.0f,
          "the key and the move in their places, the newer size last");
    program->done = true;
}

static void TestReplacedNotification(void)
{
    Program program = {.step = ReplaceStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

// A merged record counts its samples up to UINT16_MAX and stays there.
// 65 frames of 1024 reports, the most a frame takes, make 66560 moves.
enum
{
    SampleSteps = 65,
    ReportsPerFrame = 1024,
};

static void SampleStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    if (step == 1)
    {
        Drain(program, context);
    }
    if (step <= SampleSteps)
    {
        for (int i = 0; i < ReportsPerFrame; i++)
        {
            mwinEvent move = {.type = mwin_eventCursorMoved, .window = window};
            move.data.pointer.position = (mwinPosition){(float)i, (float)step};
            (void)mwinTestPost(context, &move);
        }
        return;
    }
    Drain(program, context);
    int moves = 0;
    const mwinEvent* last = nullptr;
    for (int i = 0; i < program->eventCount; i++)
    {
        moves += program->events[i].type == mwin_eventCursorMoved;
        last = program->events[i].type == mwin_eventCursorMoved ? &program->events[i] : last;
    }
    CHECK(moves == 4 && last != nullptr && last->samples == UINT16_MAX &&
              last->data.pointer.position.y == (float)SampleSteps,
          "the newest position, its samples counted up to the most");
    program->done = true;
}

static void TestSampleCount(void)
{
    Program program = {.step = SampleStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.inputPerWindow = 4;
    CHECK(RunWith(&program, def) == mwin_success, "the program runs");
}

// A composition may have MWIN_MAX_PREEDIT_SEGMENTS segments.
static void SegmentStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        const char* text = "abcdefghijklmnopqrstuvwxyz012345";
        mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
        for (uint32_t i = 0; i < MWIN_MAX_PREEDIT_SEGMENTS; i++)
        {
            segments[i] = (mwinPreeditSegment){i, 1, mwin_preeditUnderline};
        }
        mwinEvent preedit = {.type = mwin_eventImePreedit, .window = window};
        preedit.data.preedit = (mwinPreeditEvent){text,     MWIN_MAX_PREEDIT_SEGMENTS, -1, 0, 0,
                                                  segments, MWIN_MAX_PREEDIT_SEGMENTS};
        CHECK(mwinTestPost(context, &preedit) == mwin_success,
              "a composition of the most segments");
        return;
    }
    bool found = false;
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinPreeditEvent* preedit = &program->events[i].data.preedit;
        found = found || (program->events[i].type == mwin_eventImePreedit &&
                          preedit->segmentCount == MWIN_MAX_PREEDIT_SEGMENTS &&
                          preedit->segments[MWIN_MAX_PREEDIT_SEGMENTS - 1].start ==
                              MWIN_MAX_PREEDIT_SEGMENTS - 1);
    }
    CHECK(found, "it arrives with every segment");
    program->done = true;
}

static void TestMostSegments(void)
{
    Program program = {.step = SegmentStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void TouchStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        // Two moves of touch 7 fill the storage; touch 9 has nothing to join.
        for (int i = 0; i < 3; i++)
        {
            mwinEvent move = {.type = mwin_eventTouchMoved, .window = window};
            move.data.touch.id = i < 2 ? 7 : 9;
            CHECK(mwinTestPost(context, &move) == mwin_success, "touch moves");
        }
        return;
    }
    static const mwinEventType expected[] = {mwin_eventTouchMoved, mwin_eventTouchMoved,
                                             mwin_eventInputStateReset};
    CHECK(Types(program, expected, 3), "a touch never merges into another");
    program->done = true;
}

static void TestTouches(void)
{
    Program program = {.step = TouchStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.inputPerWindow = 2;
    CHECK(RunWith(&program, def) == mwin_success, "the program runs");
}

static void RefusalStep(Program* program, mwinContext* context, int step)
{
    (void)step;
    mwinWindowId window = Create(context, nullptr);
    mwinEvent bad = Text(window, "\xE0\x80\x80");
    CHECK(mwinTestPost(context, &bad) == mwin_errorInvalid, "text that is not UTF-8");
    mwinEvent reset = {.type = mwin_eventInputStateReset, .window = window};
    CHECK(mwinTestPost(context, &reset) == mwin_errorInvalid, "resets are the core's");
    CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroy");
    mwinEvent key = Key(window, mwin_eventKeyDown, mwin_codeKeyA, 'a');
    CHECK(mwinTestPost(context, &key) == mwin_errorStale, "no input for a stale window");
    CHECK(mwinMapKeyCode(context, mwin_codeKeyQ) == 'q' &&
              mwinMapKeyCode(context, mwin_codeDigit0) == '0' &&
              mwinMapKeyCode(context, mwin_codeSlash) == '/' &&
              mwinMapKeyCode(context, mwin_codeBackslash) == '\\' &&
              mwinMapKeyCode(context, mwin_codeEnter) == (MWIN_KEY_NAMED | mwin_codeEnter) &&
              mwinMapKeyCode(context, 999) == 0 && mwinMapKeyCode(nullptr, mwin_codeKeyA) == 0,
          "the layout maps codes to keys");
    char name[8];
    size_t length = 0;
    CHECK(mwinGetKeyboardLayout(context, name, sizeof(name), &length) == mwin_errorCapacity &&
              length == 12 && memcmp(name, "English ", 8) == 0,
          "the layout's name, cut to the buffer");
    program->done = true;
}

static void TestRefusalsAndLayout(void)
{
    Program program = {.step = RefusalStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void CursorStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        CHECK(mwinRequestCursorMode(context, window, mwin_cursorCaptured, &program->requests[0]) ==
                      mwin_success &&
                  mwinRequestCursorShape(context, window, mwin_shapeText, &program->requests[1]) ==
                      mwin_success,
              "cursor requests");
        CHECK(mwinRequestCursorMode(context, window, 5, nullptr) == mwin_errorInvalid &&
                  mwinRequestCursorShape(context, window, 12, nullptr) == mwin_errorInvalid,
              "unknown modes and shapes");
        return;
    }
    CHECK(program->eventCount == 2 &&
              program->events[0].data.completion.kind == mwin_requestCursorMode &&
              program->events[1].data.completion.kind == mwin_requestCursorShape &&
              program->events[1].data.completion.outcome == mwin_outcomeDone,
          "each answered");
    CHECK(mwinRequestCursorMode(context, window, mwin_cursorConfinedHidden, nullptr) ==
                  mwin_success &&
              mwinRequestCursorShape(context, window, mwin_shapeProgress, nullptr) == mwin_success,
          "the last mode and the last shape");
    program->done = true;
}

static void TestCursor(void)
{
    Program program = {.step = CursorStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static mwinKeyReach Reach(const mwinContext* context, mwinKeyCode code, mwinModifiers modifiers)
{
    mwinKeyReach reach = 0xFF;
    return mwinGetKeyReach(context, code, modifiers, &reach) == mwin_success ? reach : 0xFF;
}

static void KeyReachStep(Program* program, mwinContext* context, int step)
{
    (void)step;
    mwinKeyReach reach = 0;
    CHECK(Reach(context, mwin_codeKeyW, mwin_modControl) == mwin_keyReachDelivered,
          "every chord delivered until a test sets one");
    CHECK(mwinTestSetKeyReach(context, mwin_codeKeyW, mwin_modControl, mwin_keyReachNever) ==
                  mwin_success &&
              Reach(context, mwin_codeKeyW, mwin_modControl | mwin_modCapsLock) ==
                  mwin_keyReachNever &&
              Reach(context, mwin_codeKeyW, mwin_modControl | mwin_modShift) ==
                  mwin_keyReachDelivered &&
              Reach(context, mwin_codeKeyQ, mwin_modControl) == mwin_keyReachDelivered,
          "a set chord, the locks ignored, its neighbours untouched");
    CHECK(mwinTestSetKeyReach(context, mwin_codeKeyW, mwin_modControl, mwin_keyReachShared) ==
                  mwin_success &&
              Reach(context, mwin_codeKeyW, mwin_modControl) == mwin_keyReachShared,
          "set again, the chord's latest answer");
    CHECK(mwinTestSetKeyReach(context, mwin_codeKeyW, mwin_modControl, mwin_keyReachDelivered) ==
                  mwin_success &&
              Reach(context, mwin_codeKeyW, mwin_modControl) == mwin_keyReachDelivered,
          "delivered forgets it");
    bool filled = true;
    for (mwinKeyCode code = mwin_codeKeyA; code < mwin_codeKeyA + 32; code++)
    {
        filled = filled && mwinTestSetKeyReach(context, code, mwin_modAlt,
                                               mwin_keyReachUncertain) == mwin_success;
    }
    CHECK(filled &&
              mwinTestSetKeyReach(context, mwin_codeF1, mwin_modAlt, mwin_keyReachNever) ==
                  mwin_errorCapacity &&
              mwinTestSetKeyReach(context, mwin_codeKeyA, mwin_modAlt, mwin_keyReachNever) ==
                  mwin_success &&
              Reach(context, mwin_codeKeyA, mwin_modAlt) == mwin_keyReachNever,
          "32 chords, a set one changed when full");
    CHECK(mwinGetKeyReach(context, mwin_codeMetaRight, 0, &reach) == mwin_success,
          "an answer for the last key");
    CHECK(mwinGetKeyReach(context, mwin_codeUnknown, 0, &reach) == mwin_errorInvalid &&
              mwinGetKeyReach(context, mwin_codeMetaRight + 1, 0, &reach) == mwin_errorInvalid &&
              mwinGetKeyReach(context, mwin_codeKeyA, 0, nullptr) == mwin_errorInvalid &&
              mwinGetKeyReach(nullptr, mwin_codeKeyA, 0, &reach) == mwin_errorInvalid,
          "no answer for a code that is not a key or a NULL argument");
    CHECK(mwinTestSetKeyReach(context, mwin_codeKeyA, 0, mwin_keyReachNever + 1) ==
                  mwin_errorInvalid &&
              mwinTestSetKeyReach(context, mwin_codeUnknown, 0, mwin_keyReachNever) ==
                  mwin_errorInvalid &&
              mwinTestSetKeyReach(nullptr, mwin_codeKeyA, 0, mwin_keyReachNever) ==
                  mwin_errorInvalid,
          "no setting out of range");
    program->done = true;
}

static void TestKeyReach(void)
{
    Program program = {.step = KeyReachStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

int main(void)
{
    TestKeysAndText();
    TestTextStorage();
    TestOverflow();
    TestPenButtonOverflow();
    TestReplacedNotification();
    TestSampleCount();
    TestMostSegments();
    TestTouches();
    TestRefusalsAndLayout();
    TestCursor();
    TestKeyReach();
    return s_failures == 0 ? 0 : 1;
}
