// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's input methods in headless Chrome
// (test/web_runner.cjs), which composes through the DevTools protocol:
// accepting text moves the focus into the window's text field, at the
// caret, with no focus record; a composition as a preedit with its
// caret, its result as text and the end of the composition; a commit of
// the limit's length exactly as text, one byte past it as a reset of the
// program's text state; a key typing once; stopping ends a composition and gives the canvas the
// focus back; an on-screen keyboard's purpose.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>

#define DEADLINE_MS 10000.0
#define MAX_RECORDS 64

// "kan" in hiragana, as UTF-8.
#define KANA "\xE3\x81\x8B\xE3\x82\x93"
// The window's text bytes, and a commit of that length.
#define LIMIT 64
static const char s_limit[LIMIT + 1] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?";

typedef enum Phase
{
    phaseCreate,
    phaseFocus,
    phaseEnable,
    phaseCompose,
    phaseCommit,
    phaseExact,
    phaseTooLong,
    phaseKey,
    phaseRecompose,
    phaseDisable,
    phaseKeyboard,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    double startMs;
    mwinWindowId window;
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[LIMIT + 8];
    uint32_t textLength;
    bool timedOut;
} Program;

EM_JS_DEPS(test_web_ime, "$UTF8ToString");

// clang-format off
// The element with the focus: 1 a text field, 2 a canvas, 0 another.
EM_JS(int, Focused, (void), {
    const tag = document.activeElement && document.activeElement.tagName;
    return tag === 'TEXTAREA' ? 1 : (tag === 'CANVAS' ? 2 : 0);
});

// The field's distance from its canvas's top left, and its input mode.
EM_JS(bool, FieldAt, (float x, float y), {
    const field = document.querySelector('textarea');
    const canvas = document.querySelector('canvas:not(#page-canvas)');
    const box = canvas.getBoundingClientRect();
    return parseFloat(field.style.left) === box.left + x &&
           parseFloat(field.style.top) === box.top + y;
});

EM_JS(bool, InputModeIs, (const char* mode), {
    return document.querySelector('textarea').getAttribute('inputmode') === UTF8ToString(mode);
});
// clang-format on

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        if (event.type == mwin_eventTextInput &&
            program->textLength + event.data.text.length <= sizeof(program->text))
        {
            memcpy(program->text + program->textLength, event.data.text.text,
                   event.data.text.length);
            program->textLength += event.data.text.length;
        }
        if (event.type == mwin_eventImePreedit && event.data.preedit.length > 0)
        {
            // The composition's text, kept past the frame.
            memcpy(program->text, event.data.preedit.text, event.data.preedit.length);
            program->textLength = event.data.preedit.length;
        }
        program->records[program->count++] = event;
    }
}

static const mwinEvent* Find(const Program* program, mwinEventType type, int nth)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type && nth-- == 0)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

// The last composition record.
static const mwinEvent* Last(const Program* program)
{
    const mwinEvent* found = nullptr;
    for (int nth = 0; Find(program, mwin_eventImePreedit, nth) != nullptr; nth++)
    {
        found = Find(program, mwin_eventImePreedit, nth);
    }
    return found;
}

static bool TextIs(const Program* program, const char* text)
{
    return program->textLength == strlen(text) && memcmp(program->text, text, strlen(text)) == 0;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseFocus:
        return Find(program, mwin_eventFocusGained, 0) != nullptr;
    case phaseCompose:
    case phaseRecompose:
        return Find(program, mwin_eventImePreedit, 0) != nullptr;
    case phaseCommit:
        return Find(program, mwin_eventImePreedit, 0) != nullptr &&
               Find(program, mwin_eventTextInput, 0) != nullptr;
    case phaseExact:
        return Find(program, mwin_eventTextInput, 0) != nullptr;
    case phaseTooLong:
        return Find(program, mwin_eventInputStateReset, 0) != nullptr;
    case phaseKey:
        return Find(program, mwin_eventKeyUp, 0) != nullptr;
    case phaseDisable:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr &&
               Find(program, mwin_eventImePreedit, 0) != nullptr;
    default:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    const mwinEvent* preedit = Find(program, mwin_eventImePreedit, 0);
    mwinRect caret = {10.0f, 20.0f, 2.0f, 16.0f};
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(mwinRequestFocus(context, program->window, nullptr) == mwin_success, "focus");
        break;
    case phaseFocus:
        CHECK(mwinRequestTextInput(context, program->window, true, caret, nullptr) == mwin_success,
              "accept text");
        break;
    case phaseEnable:
        CHECK(Focused() == 1 && FieldAt(10.0f, 20.0f) &&
                  Find(program, mwin_eventFocusLost, 0) == nullptr,
              "the text field at the caret has the focus, the window keeps it");
        (void)printf("mwin-test: compose " KANA " 2 2\n");
        break;
    case phaseCompose:
        CHECK(preedit->data.preedit.length == 6 && TextIs(program, KANA) &&
                  preedit->data.preedit.caret == 6 && preedit->data.preedit.segmentCount == 1,
              "the composition and its caret");
        (void)printf("mwin-test: commit " KANA "\n");
        break;
    case phaseCommit:
        CHECK(TextIs(program, KANA) && Last(program)->data.preedit.length == 0,
              "the result as text, and the composition ends");
        (void)printf("mwin-test: commit %s\n", s_limit);
        break;
    case phaseExact:
        CHECK(TextIs(program, s_limit), "a commit of the limit's length exactly, as text");
        (void)printf("mwin-test: commit %sz\n", s_limit);
        break;
    case phaseTooLong:
        CHECK(Find(program, mwin_eventTextInput, 0) == nullptr,
              "one byte past the limit: a reset, no text");
        (void)printf("mwin-test: key KeyB\n");
        break;
    case phaseKey:
        CHECK(Find(program, mwin_eventKeyDown, 0)->data.key.code == mwin_codeKeyB &&
                  TextIs(program, "b"),
              "a key in the field, typing once");
        (void)printf("mwin-test: compose " KANA " 1 1\n");
        break;
    case phaseRecompose:
        CHECK(preedit->data.preedit.caret == 3, "a caret inside the composition");
        CHECK(mwinRequestTextInput(context, program->window, false, caret, nullptr) == mwin_success,
              "stop accepting text");
        break;
    case phaseDisable:
        CHECK(Last(program)->data.preedit.length == 0 && Focused() == 2 &&
                  Find(program, mwin_eventFocusLost, 0) == nullptr,
              "stopping ends the composition, the canvas has the focus back");
        CHECK(mwinRequestVirtualKeyboard(context, program->window, true, mwin_purposeNumber,
                                         nullptr) == mwin_success,
              "an on-screen keyboard for numbers");
        break;
    default:
        CHECK(Focused() == 1 && InputModeIs("numeric"), "the field takes numbers");
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->textLength = 0;
    program->startMs = mwinWebNow();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    program->startMs = mwinWebNow();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (mwinWebNow() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        for (int i = 0; i < program->count; i++)
        {
            (void)printf("  record %d\n", (int)program->records[i].type);
        }
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    const Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(!program->timedOut && program->phase == phaseDone, "every phase ran in time");
    (void)printf("mwin-test: exit %d\n", s_failures == 0 ? 0 : 1);
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.textBytesPerWindow = LIMIT;
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    // With Emscripten mwinRun returns only when init failed; without it,
    // it returns once init succeeded and the page's frames run the program
    // on (mwin-0022). Either way quit reports.
    if (mwinRun(&def) == mwin_success)
    {
        return 0;
    }
    (void)printf("mwin-test: exit 1\n");
    return 1;
}
