// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The input method contract against the test backend: accepting text
// by request, compositions with their caret, selection and segments,
// keys a composition consumes left out with their releases, commits as
// text, a composition ended when text input stops, refused compositions,
// offsets fitted to their text, compositions that replace each other
// without leaking storage, and a composition lost to the text limit
// ending the one before with its reset while the input method's keys
// stay left out, and control characters dropped from text and
// compositions with the offsets moved.

#include "test_program.h"

#include <math.h>
#include <string.h>

static mwinEvent Preedit(mwinWindowId window, const char* text, int32_t caret,
                         const mwinPreeditSegment* segments, uint32_t segmentCount)
{
    mwinEvent event = {.kind = mwin_eventImePreedit, .window = window};
    event.data.preedit.text = text;
    event.data.preedit.length = (uint32_t)strlen(text);
    event.data.preedit.caret = caret;
    event.data.preedit.segments = segments;
    event.data.preedit.segmentCount = segmentCount;
    return event;
}

static mwinEvent Key(mwinWindowId window, mwinEventKind type, mwinKeyCode code, mwinKey key)
{
    mwinEvent event = {.kind = type, .window = window};
    event.data.key.code = code;
    event.data.key.key = key;
    return event;
}

static void Post(mwinContext* context, mwinEvent event)
{
    CHECK(mwinContext_PostTestEvent(context, &event) == mwin_success, "a report");
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
        CHECK(mwinWindow_RequestTextInput(context, window, true,
                                          (mwinRect){10.0f, 20.0f, 1.0f, 16.0f},
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
        static const mwinEventKind expected[] = {mwin_eventKeyDown, mwin_eventImePreedit,
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
        CHECK(mwinWindow_GetState(context, window, &state) == mwin_success && state.textInput &&
                  state.composing,
              "accepting text and composing");
        bool enabled = false;
        mwinRect caret = {0};
        CHECK(mwinWindow_GetTestTextInput(context, window, &enabled, &caret) == mwin_success &&
                  enabled && caret.x == 10.0f && caret.y == 20.0f && caret.width == 1.0f &&
                  caret.height == 16.0f,
              "the test platform tells the caret it was given");
        Post(context, (mwinEvent){.kind = mwin_eventTextInput,
                                  .window = window,
                                  .data.text = {"\xE6\xBC\xA2", 3}});
        // The composition ends empty, its text NULL as some platforms
        // hand it.
        mwinEvent ended = Preedit(window, "", -1, nullptr, 0);
        ended.data.preedit.text = nullptr;
        Post(context, ended);
        Post(context, Key(window, mwin_eventKeyDown, mwin_codeKeyA, 'a'));
        return;
    }
    case 3:
        Drain(program, context);
        {
            static const mwinEventKind expected[] = {mwin_eventTextInput, mwin_eventImePreedit,
                                                     mwin_eventKeyDown};
            CHECK(Types(program, expected, 3) && program->events[1].data.preedit.length == 0,
                  "the commit as text, the end of the composition, then keys again");
        }
        Post(context, Preedit(window, "x", 1, nullptr, 0));
        return;
    case 4:
        Drain(program, context);
        CHECK(mwinWindow_RequestTextInput(context, window, false, (mwinRect){0}, nullptr) ==
                  mwin_success,
              "stop accepting text mid-composition");
        return;
    default:
        Drain(program, context);
        CHECK(program->eventCount == 2 && program->events[0].kind == mwin_eventImePreedit &&
                  program->events[0].data.preedit.length == 0 &&
                  mwinWindow_GetState(context, window, &state) == mwin_success &&
                  !state.textInput && !state.composing,
              "stopping ends the composition");
        bool enabled = true;
        mwinRect caret = {1.0f, 1.0f, 1.0f, 1.0f};
        CHECK(mwinWindow_GetTestTextInput(context, window, &enabled, &caret) == mwin_success &&
                  !enabled && caret.x == 0.0f && caret.height == 0.0f &&
                  mwinWindow_GetTestTextInput(context, window, &enabled, nullptr) ==
                      mwin_errorInvalid,
              "and the test platform tells it stopped");
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
    CHECK(mwinContext_PostTestEvent(context, &event) == mwin_errorInvalid,
          "segments without an array");
    static mwinPreeditSegment many[40];
    event = Preedit(window, "abc", 1, many, 40);
    CHECK(mwinContext_PostTestEvent(context, &event) == mwin_errorLimit,
          "more segments than MWIN_MAX_PREEDIT_SEGMENTS");
    CHECK(mwinWindow_RequestTextInput(context, window, true, (mwinRect){0.0f, 0.0f, -1.0f, 1.0f},
                                      nullptr) == mwin_errorInvalid,
          "a negative caret");
    CHECK(mwinWindow_RequestTextInput(context, window, true, (mwinRect){INFINITY, 0.0f, 1.0f, 1.0f},
                                      nullptr) == mwin_errorInvalid &&
              mwinWindow_RequestTextInput(context, window, true, (mwinRect){0.0f, NAN, 1.0f, 1.0f},
                                          nullptr) == mwin_errorInvalid,
          "a caret whose place is not finite");
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
    CHECK(mwinContext_PostTestEvent(context, &event) == mwin_success,
          "offsets that do not fit reach the core");
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
        if (program->events[i].kind == mwin_eventImePreedit)
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
        CHECK(program->events[i].kind != mwin_eventInputStateReset, "no storage leaks");
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

// A composition past the window's text storage is lost, and the reset
// in its place ends the composition the program shows. The input method
// goes on composing: the keys it takes stay left out until it ends its
// composition itself, by text input stopping, or the focus leaves.
static void LostStep(Program* program, mwinContext* context, int step)
{
    static const char* tooLong =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef!";
    mwinWindowId window = program->windows[0];
    mwinWindowState state;
    Drain(program, context);
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        return;
    case 1:
        CHECK(mwinWindow_RequestTextInput(context, window, true, (mwinRect){0}, nullptr) ==
                  mwin_success,
              "accept text");
        Post(context, Preedit(window, "ab", 2, nullptr, 0));
        return;
    case 2:
        CHECK(mwinWindow_GetState(context, window, &state) == mwin_success && state.composing,
              "composing");
        Post(context, Preedit(window, tooLong, 65, nullptr, 0));
        Post(context, Key(window, mwin_eventKeyDown, mwin_codeKeyK, 'k'));
        Post(context, Preedit(window, "abk", 3, nullptr, 0));
        Post(context, Key(window, mwin_eventKeyUp, mwin_codeKeyK, 'k'));
        return;
    case 3:
    {
        static const mwinEventKind expected[] = {mwin_eventInputStateReset, mwin_eventImePreedit};
        CHECK(Types(program, expected, 2) && program->events[1].data.preedit.length == 3,
              "the reset in the lost composition's place, then the next, without the key it took");
        Post(context, Preedit(window, tooLong, 65, nullptr, 0));
        return;
    }
    case 4:
        CHECK(program->eventCount == 1 && program->events[0].kind == mwin_eventInputStateReset,
              "the reset");
        CHECK(mwinWindow_GetState(context, window, &state) == mwin_success && !state.composing,
              "the reset ends the composition shown");
        Post(context, Key(window, mwin_eventKeyDown, mwin_codeKeyK, 'k'));
        Post(context, Key(window, mwin_eventKeyUp, mwin_codeKeyK, 'k'));
        CHECK(mwinWindow_RequestTextInput(context, window, false, (mwinRect){0}, nullptr) ==
                  mwin_success,
              "stop accepting text");
        return;
    case 5:
    {
        static const mwinEventKind expected[] = {mwin_eventImePreedit, mwin_eventRequestCompleted};
        CHECK(Types(program, expected, 2) && program->events[0].data.preedit.length == 0,
              "the key the input method took left out; its composition ends with text input");
        Post(context, Key(window, mwin_eventKeyDown, mwin_codeKeyK, 'k'));
        Post(context, Key(window, mwin_eventKeyUp, mwin_codeKeyK, 'k'));
        return;
    }
    case 6:
    {
        static const mwinEventKind expected[] = {mwin_eventKeyDown, mwin_eventKeyUp};
        CHECK(Types(program, expected, 2), "then the keys go through again");
        // A composition lost again, and the focus leaving ends it.
        Post(context, Preedit(window, tooLong, 65, nullptr, 0));
        Post(context, (mwinEvent){.kind = mwin_eventFocusLost, .window = window});
        Post(context, Key(window, mwin_eventKeyDown, mwin_codeKeyK, 'k'));
        Post(context, Key(window, mwin_eventKeyUp, mwin_codeKeyK, 'k'));
        return;
    }
    default:
    {
        static const mwinEventKind expected[] = {mwin_eventFocusLost, mwin_eventInputStateReset,
                                                 mwin_eventKeyDown, mwin_eventKeyUp};
        CHECK(Types(program, expected, 4), "the keys after the focus left go through");
        program->done = true;
    }
    }
}

static void TestLost(void)
{
    Program program = {.step = LostStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.textBytesPerWindow = 64;
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
}

// The text and composition step 1 posted, without their control
// characters.
static void CheckDropped(const Program* program)
{
    const mwinEvent* text = nullptr;
    const mwinPreeditEvent* preedit = nullptr;
    int texts = 0;
    for (int i = 0; i < program->eventCount; i++)
    {
        if (program->events[i].kind == mwin_eventTextInput)
        {
            text = &program->events[i];
            texts++;
        }
        else if (program->events[i].kind == mwin_eventImePreedit)
        {
            preedit = &program->events[i].data.preedit;
        }
    }
    CHECK(texts == 1 && text->data.text.length == 5 &&
              memcmp(text->data.text.text,
                     "ab\xC2\xA0"
                     "c",
                     5) == 0,
          "the text without its control characters, and none of text left empty");
    CHECK(preedit != nullptr && preedit->length == 3 && memcmp(preedit->text, "abc", 3) == 0,
          "the composition without its control characters");
    CHECK(preedit != nullptr && preedit->caret == 2 && preedit->selectionStart == 0 &&
              preedit->selectionEnd == 3,
          "its caret and selection moved with the bytes");
    CHECK(preedit != nullptr && preedit->segmentCount == 2 && preedit->segments[0].start == 0 &&
              preedit->segments[0].length == 2 && preedit->segments[1].start == 2 &&
              preedit->segments[1].length == 1,
          "and its segments");
}

// The commit step 2 posted, as text and keys.
static void CheckTyped(const Program* program)
{
    static const mwinEventKind expected[] = {
        mwin_eventImePreedit, mwin_eventTextInput, mwin_eventKeyDown, mwin_eventKeyUp,
        mwin_eventTextInput,  mwin_eventKeyDown,   mwin_eventKeyUp,   mwin_eventTextInput,
        mwin_eventKeyDown,    mwin_eventKeyUp};
    const mwinEvent* events = program->events;
    bool typed = Types(program, expected, 10);
    CHECK(typed && events[1].data.text.length == 1 && events[1].data.text.text[0] == 'a' &&
              events[4].data.text.length == 1 && events[4].data.text.text[0] == 'b' &&
              events[7].data.text.length == 1 && events[7].data.text.text[0] == 'c',
          "the text around the tab and the line breaks");
    CHECK(typed && events[2].data.key.code == mwin_codeTab &&
              events[3].data.key.key == (MWIN_KEY_NAMED | mwin_codeTab) &&
              events[5].data.key.code == mwin_codeEnter &&
              events[8].data.key.code == mwin_codeEnter,
          "the tab as Tab's press and release, each line break as Enter's, CR LF as one");
}

static void ControlStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    switch (step)
    {
    case 1:
    {
        // NUL, DEL and NEL (U+0085, a C1 control) go; the no-break space
        // after them stays.
        Post(context, (mwinEvent){.kind = mwin_eventTextInput,
                                  .window = window,
                                  .data.text = {"a\0b\x7F\xC2\x85\xC2\xA0"
                                                "c",
                                                9}});
        Post(context, (mwinEvent){.kind = mwin_eventTextInput,
                                  .window = window,
                                  .data.text = {"\x1B\x01", 2}});
        Post(context,
             (mwinEvent){.kind = mwin_eventTextInput, .window = window, .data.text = {"", 0}});
        // Escape, "ab", DEL, "c": the caret before "c", a selection of
        // all of it, "ab" a segment, and DEL and "c" another.
        static const mwinPreeditSegment segments[2] = {{1, 2, mwin_preeditTarget},
                                                       {3, 2, mwin_preeditUnderline}};
        mwinEvent composition = Preedit(window,
                                        "\x1B"
                                        "ab\x7F"
                                        "c",
                                        4, segments, 2);
        composition.data.preedit.selectionEnd = 5;
        Post(context, composition);
        return;
    }
    case 2:
        CheckDropped(program);
        // The composition ends; then a commit holds a tab, a carriage
        // return and line feed, and a line feed.
        Post(context, Preedit(window, "", -1, nullptr, 0));
        Post(context, (mwinEvent){.kind = mwin_eventTextInput,
                                  .window = window,
                                  .data.text = {"a\tb\r\nc\n", 7}});
        return;
    default:
        CheckTyped(program);
        program->done = true;
    }
}

static void TestControls(void)
{
    Program program = {.step = ControlStep};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
}

int main(void)
{
    TestComposition();
    TestRefusals();
    TestStorage();
    TestLost();
    TestControls();
    return s_failures == 0 ? 0 : 1;
}
