// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The input method contract against the test backend: accepting text
// by request, compositions with their caret, selection and segments,
// keys a composition consumes left out with their releases, commits as
// text, a composition ended when text input stops, refused compositions,
// offsets fitted to their text, and compositions that replace each
// other without leaking storage.

#include "test_program.h"

#include <string.h>

static mwinEvent Preedit(mwinWindowId window, const char* text, int32_t caret,
                         const mwinPreeditSegment* segments, uint32_t segmentCount)
{
    mwinEvent event = {.type = mwin_eventImePreedit, .window = window};
    event.data.preedit.text = text;
    event.data.preedit.length = (uint32_t)strlen(text);
    event.data.preedit.caret = caret;
    event.data.preedit.segments = segments;
    event.data.preedit.segmentCount = segmentCount;
    return event;
}

static mwinEvent Key(mwinWindowId window, mwinEventType type, mwinKeyCode code, mwinKey key)
{
    mwinEvent event = {.type = type, .window = window};
    event.data.key.code = code;
    event.data.key.key = key;
    return event;
}

static void Post(mwinContext* context, mwinEvent event)
{
    CHECK(mwinTestPost(context, &event) == mwin_success, "a report");
}

static void ComposeStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    mwinWindowState state;
    static const mwinPreeditSegment segments[2] = {{0, 3, mwin_preeditTarget},
                                                   {3, 3, mwin_preeditUnderline}};
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        return;
    case 1:
        Drain(program, context);
        CHECK(mwinRequestTextInput(context, window, true, (mwinRect){10.0f, 20.0f, 1.0f, 16.0f},
                                   nullptr) == mwin_success,
              "accept text");
        Post(context, Key(window, mwin_eventKeyDown, mwin_codeKeyB, 'b'));
        Post(context, Preedit(window, "\xE3\x81\x8B\xE3\x82\x93", 6, segments, 2));
        Post(context, Key(window, mwin_eventKeyDown, mwin_codeKeyK, 'k'));
        Post(context, Key(window, mwin_eventKeyUp, mwin_codeKeyK, 'k'));
        Post(context, Key(window, mwin_eventKeyUp, mwin_codeKeyB, 'b'));
        Post(context,
             Key(window, mwin_eventKeyDown, mwin_codeEnter, MWIN_KEY_NAMED | mwin_codeEnter));
        return;
    case 2:
    {
        Drain(program, context);
        static const mwinEventType expected[] = {mwin_eventKeyDown, mwin_eventImePreedit,
                                                 mwin_eventKeyUp, mwin_eventKeyDown,
                                                 mwin_eventRequestCompleted};
        CHECK(Types(program, expected, 5), "the composition consumes its character keys");
        CHECK(program->events[2].data.key.code == mwin_codeKeyB &&
                  program->events[3].data.key.code == mwin_codeEnter,
              "a key held from before is released; keys that type nothing pass");
        const mwinPreeditEvent* preedit = &program->events[1].data.preedit;
        CHECK(preedit->length == 6 && memcmp(preedit->text, "\xE3\x81\x8B\xE3\x82\x93", 6) == 0 &&
                  preedit->caret == 6 && preedit->segmentCount == 2 &&
                  preedit->segments[1].start == 3 &&
                  preedit->segments[0].style == mwin_preeditTarget,
              "the composition, its caret and its segments");
        CHECK(mwinGetWindowState(context, window, &state) == mwin_success && state.textInput &&
                  state.composing,
              "accepting text and composing");
        Post(context, (mwinEvent){.type = mwin_eventTextInput,
                                  .window = window,
                                  .data.text = {"\xE6\xBC\xA2", 3}});
        Post(context, Preedit(window, "", -1, nullptr, 0));
        Post(context, Key(window, mwin_eventKeyDown, mwin_codeKeyA, 'a'));
        return;
    }
    case 3:
        Drain(program, context);
        {
            static const mwinEventType expected[] = {mwin_eventTextInput, mwin_eventImePreedit,
                                                     mwin_eventKeyDown};
            CHECK(Types(program, expected, 3) && program->events[1].data.preedit.length == 0,
                  "the commit as text, the end of the composition, then keys again");
        }
        Post(context, Preedit(window, "x", 1, nullptr, 0));
        return;
    case 4:
        Drain(program, context);
        CHECK(mwinRequestTextInput(context, window, false, (mwinRect){0}, nullptr) == mwin_success,
              "stop accepting text mid-composition");
        return;
    default:
        Drain(program, context);
        CHECK(program->eventCount == 2 && program->events[0].type == mwin_eventImePreedit &&
                  program->events[0].data.preedit.length == 0 &&
                  mwinGetWindowState(context, window, &state) == mwin_success && !state.textInput &&
                  !state.composing,
              "stopping ends the composition");
        program->done = true;
    }
}

static void TestComposition(void)
{
    Program program = {.step = ComposeStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void RefusalStep(Program* program, mwinContext* context, int step)
{
    (void)step;
    mwinWindowId window = Create(context, nullptr);
    mwinEvent event = Preedit(window, "abc", 1, nullptr, 3);
    CHECK(mwinTestPost(context, &event) == mwin_errorInvalid, "segments without an array");
    static mwinPreeditSegment many[40];
    event = Preedit(window, "abc", 1, many, 40);
    CHECK(mwinTestPost(context, &event) == mwin_errorInvalid, "too many segments");
    CHECK(mwinRequestTextInput(context, window, true, (mwinRect){0.0f, 0.0f, -1.0f, 1.0f},
                               nullptr) == mwin_errorInvalid,
          "a negative caret");
    // "a", U+3042 in three bytes, "b": a caret inside the character, a
    // selection backwards and past the end, a segment reaching into the
    // character, one past the text and one empty.
    static const mwinPreeditSegment segments[3] = {
        {2, 1, mwin_preeditTarget}, {5, 4, mwin_preeditUnderline}, {0, 0, mwin_preeditPlain}};
    event = Preedit(window,
                    "a\xE3\x81\x82"
                    "b",
                    2, segments, 3);
    event.data.preedit.selectionStart = 9;
    event.data.preedit.selectionEnd = 3;
    CHECK(mwinTestPost(context, &event) == mwin_success, "offsets that do not fit reach the core");
    program->done = true;
}

static void FittedStep(Program* program, mwinContext* context, int step)
{
    if (step == 0)
    {
        RefusalStep(program, context, step);
        program->done = false;
        return;
    }
    Drain(program, context);
    const mwinPreeditEvent* fitted = nullptr;
    for (int i = 0; i < program->eventCount; i++)
    {
        if (program->events[i].type == mwin_eventImePreedit)
        {
            fitted = &program->events[i].data.preedit;
        }
    }
    CHECK(fitted != nullptr && fitted->length == 5, "the composition kept");
    CHECK(fitted != nullptr && fitted->caret == 1, "the caret at the character's start");
    CHECK(fitted != nullptr && fitted->selectionStart == 1 && fitted->selectionEnd == 5,
          "the selection in order, within the text, covering the character");
    CHECK(fitted != nullptr && fitted->segmentCount == 1 && fitted->segments[0].start == 1 &&
              fitted->segments[0].length == 3 && fitted->segments[0].style == mwin_preeditTarget,
          "the segment covering the character, the empty ones dropped");
    program->done = true;
}

static void TestRefusals(void)
{
    Program program = {.step = FittedStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void StorageStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    for (int i = 0; i < program->eventCount; i++)
    {
        CHECK(program->events[i].type != mwin_eventInputStateReset, "no storage leaks");
    }
    CHECK(step == 1 || (program->eventCount == 1 && program->events[0].data.preedit.length == 10),
          "only the newest composition of a pump");
    static const mwinPreeditSegment segment = {0, 10, mwin_preeditUnderline};
    Post(context, Preedit(window, "abcdefghij", 10, &segment, 1));
    Post(context, Preedit(window, "0123456789", 10, &segment, 1));
    program->done = step == 60;
}

static void TestStorage(void)
{
    Program program = {.step = StorageStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.textBytesPerWindow = 64;
    CHECK(RunWith(&program, def) == mwin_success, "the program runs");
}

int main(void)
{
    TestComposition();
    TestRefusals();
    TestStorage();
    return s_failures == 0 ? 0 : 1;
}
