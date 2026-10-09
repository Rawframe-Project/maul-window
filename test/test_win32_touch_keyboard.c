// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's touch keyboard: asked to show for a number field
// and to hide, each request completes, and on Windows 10 1607 or later
// the window's InputPane answers (done where the keyboard shows, denied
// where a hardware keyboard is attached), so the request is never
// unsupported there. Under wine, which may have no InputPane, it may be.
// The window then follows the keyboard through IFrameworkInputPane (on
// Windows; wine may lack it): its handler, called as Windows would with
// the keyboard over the lower part of the client area, reports the part
// covered in logical units, once however often it is told; shown wholly
// below the window or hidden, it reports none. The handler answers no
// interface but its own.

#include "test_harness.h"
#include "win32.h"

#include "maul-window/event.h"
#include "maul-window/input.h"

#include <math.h>
#include <shobjidl.h>
#include <string.h>

#define DEADLINE_MS 10000u

typedef enum Phase
{
    phaseCreate,
    phaseShow,
    phaseCovered,
    phaseOutside,
    phaseAgain,
    phaseUncovered,
    phaseHide,
    phaseDone,
} Phase;

typedef struct Program
{
    ULONGLONG startMs;
    mwinWindowId window;
    mwinRequestId request;
    Phase phase;
    bool wine;
    int outcomes[2];
    bool advised;
    float scale;
    float width;
    float height;
    mwinRect covered[4];
    int covers;
    bool timedOut;
} Program;

// The outcome of the request's completion, drained from the stream, or
// -1; whether the window was made, in *created; the covered parts
// reported, in the program.
static int Outcome(Program* program, mwinContext* context, mwinRequestId request, bool* created)
{
    mwinEvent event;
    int outcome = -1;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        *created = *created || event.type == mwin_eventWindowCreated;
        if (event.type == mwin_eventVirtualKeyboardChanged && program->covers < 4)
        {
            program->covered[program->covers++] = event.data.rect;
        }
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == request.index1 &&
            completion->request.generation == request.generation)
        {
            outcome = completion->outcome;
        }
    }
    return outcome;
}

static bool Answered(int outcome, bool wine)
{
    return outcome == mwin_outcomeDone || outcome == mwin_outcomeDenied ||
           (wine && outcome == mwin_outcomeUnsupported);
}

// The window's handler, called as Windows would call it.
static IFrameworkInputPaneHandler* Handler(Program* program, mwinContext* context,
                                           mwinWin32Window** windowOut)
{
    mwinWin32Window* window =
        &((mwinWin32Platform*)context->backendData)->windows[program->window.index1 - 1];
    *windowOut = window;
    return (IFrameworkInputPaneHandler*)&window->paneHandler;
}

// The keyboard over the client area's lower half, wider than the
// window, or wholly below it.
static void Cover(Program* program, mwinContext* context, bool over)
{
    mwinWin32Window* window = nullptr;
    IFrameworkInputPaneHandler* handler = Handler(program, context, &window);
    program->advised = window->paneHandler.pane != nullptr;
    POINT origin = {0, 0};
    (void)ClientToScreen(window->hwnd, &origin);
    LONG top = origin.y + (over ? (LONG)window->height / 2 : (LONG)window->height + 10);
    RECT keyboard = {origin.x - 50, top, origin.x + (LONG)window->width + 50,
                     origin.y + (LONG)window->height + 200};
    (void)handler->lpVtbl->Showing(handler, &keyboard, TRUE);
    // Told again where it already is: nothing new to report.
    (void)handler->lpVtbl->Showing(handler, &keyboard, TRUE);
    void* other = &keyboard;
    void* self = nullptr;
    CHECK(handler->lpVtbl->QueryInterface(handler, &IID_IDataObject, &other) == E_NOINTERFACE &&
              other == nullptr &&
              handler->lpVtbl->QueryInterface(handler, &IID_IUnknown, &self) == S_OK &&
              self == handler,
          "the handler answers no interface but its own");
    program->scale = (float)window->dpi / 96.0f;
    program->width = (float)window->width / program->scale;
    program->height = (float)window->height / program->scale;
}

static void Uncover(Program* program, mwinContext* context)
{
    mwinWin32Window* window = nullptr;
    IFrameworkInputPaneHandler* handler = Handler(program, context, &window);
    (void)handler->lpVtbl->Hiding(handler, TRUE);
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startMs = GetTickCount64();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    bool created = false;
    int outcome = Outcome(program, context, program->request, &created);
    bool advance = false;
    switch (program->phase)
    {
    case phaseCreate:
        advance = created;
        break;
    case phaseCovered:
    case phaseOutside:
    case phaseAgain:
    case phaseUncovered:
        advance = program->covers >= (int)(program->phase - phaseShow);
        break;
    default:
        advance = outcome >= 0;
        break;
    }
    if (advance)
    {
        switch (program->phase)
        {
        case phaseShow:
            program->outcomes[0] = outcome;
            Cover(program, context, true);
            break;
        case phaseCovered:
            Cover(program, context, false);
            break;
        case phaseOutside:
            Cover(program, context, true);
            break;
        case phaseAgain:
            Uncover(program, context);
            break;
        case phaseHide:
            program->outcomes[1] = outcome;
            break;
        default:
            break;
        }
        if (program->phase == phaseCreate || program->phase == phaseUncovered)
        {
            bool show = program->phase == phaseCreate;
            CHECK(mwinRequestVirtualKeyboard(context, program->window, show, mwin_purposeNumber,
                                             &program->request) == mwin_success,
                  show ? "asked to show" : "asked to hide");
        }
        program->phase += 1;
        program->startMs = GetTickCount64();
    }
    else if (GetTickCount64() - program->startMs > DEADLINE_MS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        Sleep(1);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    static Program program;
    program.wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && !program.timedOut && program.phase == phaseDone,
          "the program runs to its end");
    CHECK(Answered(program.outcomes[0], program.wine), "showing answered by the InputPane");
    CHECK(Answered(program.outcomes[1], program.wine), "hiding answered by the InputPane");
    CHECK(program.advised || program.wine, "the window follows the keyboard on Windows");
    const mwinRect* shown = &program.covered[0];
    float half = (float)(int)(program.height * program.scale / 2.0f) / program.scale;
    CHECK(program.covers == 4 && shown->x == 0.0f && shown->width == program.width &&
              fabsf(shown->y - half) < 0.01f &&
              fabsf(shown->y + shown->height - program.height) < 0.01f &&
              memcmp(&program.covered[2], shown, sizeof(*shown)) == 0,
          "shown over the window, the part of the client area it covers");
    CHECK(program.covers == 4 && program.covered[1].width == 0.0f &&
              program.covered[1].height == 0.0f,
          "shown below the window, none");
    CHECK(program.covers == 4 && program.covered[3].width == 0.0f &&
              program.covered[3].height == 0.0f,
          "hidden, none");
    return s_failures == 0 ? 0 : 1;
}
