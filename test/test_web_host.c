// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's accessibility host in headless Chrome
// (test/web_runner.cjs): the focus moving from the canvas to an element
// in the host, as a screen reader moves it, and back is no change of
// focus; a key there reaches the program with its text, a key in a field
// or a text area there reaches it without its text, which the element
// keeps; a focus
// request leaves the focus on the element; the focus leaving for the
// page is lost, and coming back to the host gained.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>

#define DEADLINE_MS 10000.0
#define MAX_RECORDS 64

typedef enum Phase
{
    phaseCreate,
    phaseFocus,
    phaseButton,
    phaseField,
    phaseArea,
    phaseRequest,
    phaseLeave,
    phaseReturn,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    double startMs;
    mwinWindowId window;
    char host[128];
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[16];
    uint32_t textLength;
    bool timedOut;
} Program;

EM_JS_DEPS(test_web_host, "$UTF8ToString");

// clang-format off
// An adapter's elements in the host: a button the focus can reach, a
// field and a text area, as a screen reader's focus mode has them; and a
// button on the page outside the window.
EM_JS(void, MakeElements, (const char* host), {
    const parent = document.querySelector(UTF8ToString(host));
    const button = document.createElement('div');
    button.id = 'host-button';
    button.setAttribute('role', 'button');
    button.tabIndex = -1;
    const field = document.createElement('input');
    field.id = 'host-field';
    const area = document.createElement('textarea');
    area.id = 'host-area';
    parent.append(button, field, area);
    const outside = document.createElement('button');
    outside.id = 'outside';
    document.body.append(outside);
});

EM_JS(void, FocusElement, (const char* id), {
    document.getElementById(UTF8ToString(id)).focus();
});

EM_JS(bool, FocusIs, (const char* id), {
    return document.activeElement === document.getElementById(UTF8ToString(id));
});

EM_JS(bool, ValueIs, (const char* id, const char* text), {
    return document.getElementById(UTF8ToString(id)).value === UTF8ToString(text);
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

static const mwinEvent* Find(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

static bool FocusUnchanged(const Program* program)
{
    return Find(program, mwin_eventFocusGained) == nullptr &&
           Find(program, mwin_eventFocusLost) == nullptr;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseFocus:
        return Find(program, mwin_eventFocusGained) != nullptr;
    case phaseButton:
    case phaseField:
    case phaseArea:
        return Find(program, mwin_eventKeyUp) != nullptr;
    case phaseLeave:
        return Find(program, mwin_eventFocusLost) != nullptr;
    case phaseReturn:
        return Find(program, mwin_eventFocusGained) != nullptr;
    default:
        return Find(program, mwin_eventRequestCompleted) != nullptr;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    const mwinEvent* down = Find(program, mwin_eventKeyDown);
    const mwinEvent* completed = Find(program, mwin_eventRequestCompleted);
    switch (program->phase)
    {
    case phaseCreate:
    {
        mwinNativeHandles handles;
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success &&
                  handles.handles.web.accessibilityLength < sizeof(program->host),
              "the accessibility host");
        memcpy(program->host, handles.handles.web.accessibility,
               handles.handles.web.accessibilityLength);
        program->host[handles.handles.web.accessibilityLength] = '\0';
        MakeElements(program->host);
        CHECK(mwinRequestFocus(context, program->window, nullptr) == mwin_success, "focus");
        break;
    }
    case phaseFocus:
        FocusElement("host-button");
        (void)printf("mwin-test: key KeyB\n");
        break;
    case phaseButton:
        CHECK(FocusUnchanged(program), "the focus moved into the host: no change");
        CHECK(down != nullptr && down->data.key.code == mwin_codeKeyB && program->textLength == 1 &&
                  program->text[0] == 'b',
              "a key on the host's button, with its text");
        FocusElement("host-field");
        (void)printf("mwin-test: key KeyC\n");
        break;
    case phaseField:
        CHECK(FocusUnchanged(program), "the focus moved within the host: no change");
        CHECK(down != nullptr && down->data.key.code == mwin_codeKeyC && program->textLength == 0 &&
                  ValueIs("host-field", "c"),
              "a key in the host's field, its text the field's");
        FocusElement("host-area");
        (void)printf("mwin-test: key KeyD\n");
        break;
    case phaseArea:
        CHECK(FocusUnchanged(program), "the focus moved to the text area: no change");
        CHECK(down != nullptr && down->data.key.code == mwin_codeKeyD && program->textLength == 0 &&
                  ValueIs("host-area", "d"),
              "a key in the host's text area, its text the area's");
        CHECK(mwinRequestFocus(context, program->window, nullptr) == mwin_success,
              "focus while in the host");
        break;
    case phaseRequest:
        CHECK(completed->data.completion.outcome == mwin_outcomeDone && FocusIs("host-area") &&
                  FocusUnchanged(program),
              "a focus request leaves the focus in the host");
        FocusElement("outside");
        break;
    case phaseLeave:
        CHECK(FocusIs("outside"), "the focus left for the page is lost");
        FocusElement("host-button");
        break;
    default:
        CHECK(FocusIs("host-button") && Find(program, mwin_eventFocusLost) == nullptr,
              "the focus coming back to the host is gained");
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
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    // With Emscripten mwinRun returns only when init failed; without it,
    // it returns once init succeeded and the page's frames run the program
    // on (mwin-0022).
    if (mwinRun(&def) == mwin_success)
    {
        return 0;
    }
    (void)printf("mwin-test: exit 1\n");
    return 1;
}
