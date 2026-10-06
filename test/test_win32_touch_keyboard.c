// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's touch keyboard: asked to show for a number field
// and to hide, each request completes, and on Windows 10 1607 or later
// the window's InputPane answers (done where the keyboard shows, denied
// where a hardware keyboard is attached), so the request is never
// unsupported there. Under wine, which may have no InputPane, it may be.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/input.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 10000u

typedef enum Phase
{
    phaseCreate,
    phaseShow,
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
    bool timedOut;
} Program;

// The outcome of the request's completion, drained from the stream, or
// -1; whether the window was made, in *created.
static int Outcome(mwinContext* context, mwinRequestId request, bool* created)
{
    mwinEvent event;
    int outcome = -1;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        *created = *created || event.type == mwin_eventWindowCreated;
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
    int outcome = Outcome(context, program->request, &created);
    bool advance = program->phase == phaseCreate ? created : outcome >= 0;
    if (advance)
    {
        if (program->phase != phaseCreate)
        {
            program->outcomes[program->phase - phaseShow] = outcome;
        }
        if (program->phase != phaseHide)
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
    return s_failures == 0 ? 0 : 1;
}
