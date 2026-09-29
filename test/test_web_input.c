// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's input in headless Chrome (test/web_runner.cjs),
// which types, moves the mouse, touches and draws with a pen as the test
// asks: keys with their codes, meanings and text, Shift, a repeat, a
// named key, the pointer entering and moving, a double click, the wheel,
// cursor shapes, a hidden cursor, no confinement, a touch, a pen, and a
// captured cursor with raw motion after a click.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>

#define DEADLINE_MS 10000.0
#define MAX_RECORDS 128

typedef enum Phase
{
    phaseCreate,
    phaseFocus,
    phaseKeys,
    phaseShift,
    phaseRepeat,
    phaseNamed,
    phasePointer,
    phaseClicks,
    phaseWheel,
    phaseShape,
    phaseHidden,
    phaseConfined,
    phaseTouch,
    phasePen,
    phaseClick,
    phaseCapture,
    phaseRaw,
    phaseRelease,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    double startMs;
    mwinWindowId window;
    char selector[128];
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[64];
    uint32_t textLength;
    bool timedOut;
} Program;

EM_JS_DEPS(test_web_input, "$UTF8ToString");

// clang-format off
EM_JS(bool, CursorIs, (const char* selector, const char* cursor), {
    return document.querySelector(UTF8ToString(selector)).style.cursor === UTF8ToString(cursor);
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

static bool TextIs(const Program* program, const char* text)
{
    return program->textLength == strlen(text) && memcmp(program->text, text, strlen(text)) == 0;
}

// Asks the runner for something, about the window's canvas where it
// names one.
static void Ask(const Program* program, const char* command, const char* rest)
{
    (void)printf("mwin-test: %s %s%s\n", command, rest[0] == ' ' ? program->selector : "", rest);
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    case phaseFocus:
        return Find(program, mwin_eventFocusGained, 0) != nullptr;
    case phaseKeys:
    case phaseNamed:
        return Find(program, mwin_eventKeyUp, 0) != nullptr;
    case phaseShift:
        return Find(program, mwin_eventKeyUp, 1) != nullptr;
    case phaseRepeat:
        return Find(program, mwin_eventKeyUp, 0) != nullptr;
    case phasePointer:
        return Find(program, mwin_eventCursorMoved, 0) != nullptr;
    case phaseClicks:
        return Find(program, mwin_eventButtonUp, 1) != nullptr;
    case phaseWheel:
        return Find(program, mwin_eventWheel, 0) != nullptr;
    case phaseTouch:
        return Find(program, mwin_eventTouchUp, 0) != nullptr;
    case phasePen:
        return Find(program, mwin_eventPenUp, 0) != nullptr;
    case phaseClick:
        return Find(program, mwin_eventButtonUp, 0) != nullptr;
    case phaseRaw:
        return Find(program, mwin_eventRawPointerDelta, 0) != nullptr;
    default:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    }
}

// The keyboard's phases.
static void AdvanceKeys(Program* program, mwinContext* context)
{
    const mwinEvent* down = Find(program, mwin_eventKeyDown, 0);
    switch (program->phase)
    {
    case phaseFocus:
        Ask(program, "key", "KeyA");
        break;
    case phaseKeys:
        CHECK(down != nullptr && down->data.key.code == mwin_codeKeyA &&
                  down->data.key.key == 'a' && !down->data.key.repeat && TextIs(program, "a"),
              "a key, what it means, and its text");
        CHECK(mwinMapKeyCode(context, mwin_codeKeyQ) == 'q' &&
                  mwinMapKeyCode(context, mwin_codeEscape) == (MWIN_KEY_NAMED | mwin_codeEscape),
              "what keys mean in the layout");
        Ask(program, "down", "ShiftLeft");
        Ask(program, "key", "KeyA");
        Ask(program, "up", "ShiftLeft");
        break;
    case phaseShift:
    {
        const mwinEvent* a = Find(program, mwin_eventKeyDown, 1);
        CHECK(down->data.key.code == mwin_codeShiftLeft && a != nullptr &&
                  a->data.key.code == mwin_codeKeyA && a->data.key.key == 'a' &&
                  (a->data.key.modifiers & mwin_modShift) != 0 && TextIs(program, "A"),
              "Shift changes the text, not the meaning");
        Ask(program, "down", "KeyA");
        Ask(program, "down", "KeyA");
        Ask(program, "up", "KeyA");
        break;
    }
    case phaseRepeat:
        CHECK(Find(program, mwin_eventKeyDown, 1) != nullptr && !down->data.key.repeat &&
                  Find(program, mwin_eventKeyDown, 1)->data.key.repeat && TextIs(program, "aa"),
              "a repeat, typing again");
        Ask(program, "key", "Enter");
        break;
    default:
        CHECK(down != nullptr && down->data.key.key == (MWIN_KEY_NAMED | mwin_codeEnter) &&
                  program->textLength == 0,
              "a named key, typing nothing");
        Ask(program, "move", " 100 50");
        break;
    }
}

// The pointer's and the cursor's phases.
static void AdvancePointer(Program* program, mwinContext* context)
{
    const mwinEvent* completed = Find(program, mwin_eventRequestCompleted, 0);
    mwinOutcome outcome = completed != nullptr ? completed->data.completion.outcome : 0;
    switch (program->phase)
    {
    case phasePointer:
    {
        const mwinEvent* moved = Find(program, mwin_eventCursorMoved, 0);
        CHECK(Find(program, mwin_eventCursorEntered, 0) != nullptr &&
                  moved->data.pointer.position.x == 100.0f &&
                  moved->data.pointer.position.y == 50.0f,
              "the pointer enters and moves in CSS pixels");
        for (int i = 0; i < 2; i++)
        {
            Ask(program, "press", "left");
            Ask(program, "release", "left");
        }
        break;
    }
    case phaseClicks:
        CHECK(Find(program, mwin_eventButtonDown, 0)->data.pointer.clicks == 1 &&
                  Find(program, mwin_eventButtonDown, 1)->data.pointer.clicks == 2 &&
                  Find(program, mwin_eventButtonDown, 0)->data.pointer.button == mwin_buttonLeft &&
                  Find(program, mwin_eventButtonDown, 0)->data.pointer.buttons == 1 &&
                  Find(program, mwin_eventButtonUp, 1)->data.pointer.buttons == 0,
              "a quick second click is a double click");
        // The protocol's deltas are in device pixels: 200 at the runner's
        // ratio of 2 is 100 CSS pixels, a detent.
        Ask(program, "wheel", "0 200");
        break;
    case phaseWheel:
        CHECK(Find(program, mwin_eventWheel, 0)->data.wheel.y == -1.0f &&
                  Find(program, mwin_eventWheel, 0)->data.wheel.x == 0.0f,
              "a detent toward the user");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a text cursor");
        break;
    case phaseShape:
        CHECK(outcome == mwin_outcomeDone && CursorIs(program->selector, "text"),
              "the text cursor over the canvas");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorHidden, nullptr) ==
                  mwin_success,
              "hide");
        break;
    case phaseHidden:
        CHECK(outcome == mwin_outcomeDone && CursorIs(program->selector, "none"), "hidden");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorConfined, nullptr) ==
                  mwin_success,
              "confine");
        break;
    default:
        CHECK(outcome == mwin_outcomeUnsupported, "a page cannot confine the pointer");
        Ask(program, "touch", " 50 60");
        break;
    }
}

// The captured cursor's phases: a click first, the user's gesture the
// browser asks for.
static void AdvanceCapture(Program* program, mwinContext* context)
{
    const mwinEvent* completed = Find(program, mwin_eventRequestCompleted, 0);
    mwinOutcome outcome = completed != nullptr ? completed->data.completion.outcome : 0;
    switch (program->phase)
    {
    case phaseClick:
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorCaptured, nullptr) ==
                  mwin_success,
              "capture");
        break;
    case phaseCapture:
        CHECK(outcome == mwin_outcomeDone, "the pointer locked");
        Ask(program, "move", " 110 45");
        break;
    case phaseRaw:
    {
        const mwinEvent* delta = Find(program, mwin_eventRawPointerDelta, 0);
        CHECK(delta->data.delta.x == 10.0f && delta->data.delta.y == -5.0f &&
                  Find(program, mwin_eventCursorMoved, 0) == nullptr,
              "raw motion while captured, no cursor");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorVisible, nullptr) ==
                  mwin_success,
              "release");
        break;
    }
    default:
        CHECK(outcome == mwin_outcomeDone && CursorIs(program->selector, "text"),
              "released: the text cursor again");
        break;
    }
}

static void AdvanceContacts(Program* program)
{
    if (program->phase == phaseTouch)
    {
        const mwinEvent* down = Find(program, mwin_eventTouchDown, 0);
        const mwinEvent* moved = Find(program, mwin_eventTouchMoved, 0);
        const mwinEvent* up = Find(program, mwin_eventTouchUp, 0);
        CHECK(down != nullptr && down->data.touch.position.x == 50.0f &&
                  down->data.touch.position.y == 60.0f && down->data.touch.pressure == 0.5f,
              "a touch down where it lands, with its pressure");
        CHECK(down != nullptr && moved != nullptr && moved->data.touch.id == down->data.touch.id &&
                  moved->data.touch.position.x == 60.0f && up->data.touch.id == down->data.touch.id,
              "the touch moves and lifts, the same id");
        CHECK(Find(program, mwin_eventButtonDown, 0) == nullptr, "no mouse made of the touch");
        Ask(program, "pen", " 200 150");
        return;
    }
    const mwinEvent* pressed = Find(program, mwin_eventPenButtonDown, 0);
    const mwinEvent* down = Find(program, mwin_eventPenDown, 0);
    CHECK(pressed != nullptr && pressed->data.pen.button == 1 &&
              (pressed->data.pen.flags & mwin_penContact) == 0,
          "the barrel button, the pen hovering");
    CHECK(down != nullptr && down->data.pen.pressure == 0.25f && down->data.pen.tiltX == 30.0f &&
              down->data.pen.tiltY == -15.0f &&
              down->data.pen.flags == (mwin_penContact | mwin_penBarrel) &&
              down->data.pen.position.x == 200.0f,
          "the pen down with its pressure and tilt");
    CHECK(Find(program, mwin_eventPenButtonUp, 0) != nullptr &&
              (Find(program, mwin_eventPenUp, 0)->data.pen.flags & mwin_penContact) == 0,
          "the pen up, its button let go");
    Ask(program, "click", " 100 50");
}

static void Advance(Program* program, mwinContext* context)
{
    if (program->phase == phaseCreate)
    {
        mwinNativeHandles handles;
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success &&
                  handles.handles.web.selectorLength < sizeof(program->selector),
              "the canvas");
        memcpy(program->selector, handles.handles.web.selector, handles.handles.web.selectorLength);
        program->selector[handles.handles.web.selectorLength] = '\0';
        CHECK(mwinRequestFocus(context, program->window, nullptr) == mwin_success, "focus");
    }
    else if (program->phase <= phaseNamed)
    {
        AdvanceKeys(program, context);
    }
    else if (program->phase <= phaseConfined)
    {
        AdvancePointer(program, context);
    }
    else if (program->phase <= phasePen)
    {
        AdvanceContacts(program);
    }
    else
    {
        AdvanceCapture(program, context);
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
