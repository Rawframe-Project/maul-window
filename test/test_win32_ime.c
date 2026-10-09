// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's input methods, driven through the input context:
// no context until the window accepts text, a composition as a preedit
// with its caret and clause, its result as text and the end of the
// composition (the result once, the text state kept), and a composition
// cancelled when the window stops accepting text. Where the layout has
// no input method to compose with (an English Windows), the test is
// skipped (exit status 77).

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// After windows.h, whose types it uses.
#include <imm.h>

#define DEADLINE_MS 10000u
#define MAX_RECORDS 64

// "kan" in hiragana, and as UTF-8.
#define KANA      L"\x304B\x3093"
#define KANA_UTF8 "\xE3\x81\x8B\xE3\x82\x93"

typedef enum Phase
{
    phaseCreate,
    phaseEnable,
    phaseCompose,
    phaseCommit,
    phaseRecompose,
    phaseDisable,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    ULONGLONG startMs;
    mwinWindowId window;
    HWND hwnd;
    mwinEvent records[MAX_RECORDS];
    int count;
    bool skipped;
    bool timedOut;
} Program;

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
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

// Composes kana in the window's input context: false where no input
// method takes it.
static bool Compose(const Program* program)
{
    HIMC context = ImmGetContext(program->hwnd);
    bool composed =
        context != nullptr && ImmSetCompositionStringW(context, SCS_SETSTR, KANA,
                                                       sizeof(KANA) - sizeof(WCHAR), nullptr, 0);
    ImmReleaseContext(program->hwnd, context);
    return composed;
}

static void Complete(const Program* program)
{
    HIMC context = ImmGetContext(program->hwnd);
    CHECK(ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_COMPLETE, 0), "the composition completes");
    ImmReleaseContext(program->hwnd, context);
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventShown, 0) != nullptr;
    case phaseCompose:
    case phaseRecompose:
        return Find(program, mwin_eventImePreedit, 0) != nullptr;
    case phaseCommit:
        return Find(program, mwin_eventTextInput, 0) != nullptr &&
               Find(program, mwin_eventImePreedit, 0) != nullptr;
    case phaseDisable:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr &&
               Find(program, mwin_eventImePreedit, 0) != nullptr;
    default:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    }
}

static bool Composing(const mwinPreeditEvent* preedit)
{
    return preedit->length == 6 && memcmp(preedit->text, KANA_UTF8, 6) == 0 &&
           preedit->caret == 6 && preedit->segmentCount == 1 && preedit->segments[0].start == 0 &&
           preedit->segments[0].length == 6;
}

static void Advance(Program* program, mwinContext* context)
{
    const mwinEvent* preedit = Find(program, mwin_eventImePreedit, 0);
    mwinRect caret = {10.0f, 20.0f, 2.0f, 16.0f};
    switch (program->phase)
    {
    case phaseCreate:
    {
        mwinNativeHandles handles;
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success,
              "the window's HWND");
        program->hwnd = handles.handles.win32.hwnd;
        CHECK(ImmGetContext(program->hwnd) == nullptr, "no input context before text is asked");
        SetForegroundWindow(program->hwnd);
        CHECK(mwinRequestTextInput(context, program->window, true, caret, nullptr) == mwin_success,
              "accept text");
        break;
    }
    case phaseEnable:
        if (!Compose(program))
        {
            program->skipped = true;
            program->phase = phaseDone;
            return;
        }
        break;
    case phaseCompose:
        CHECK(Composing(&preedit->data.preedit), "the composition, its caret and its clause");
        Complete(program);
        break;
    case phaseCommit:
    {
        const mwinTextEvent* text = &Find(program, mwin_eventTextInput, 0)->data.text;
        CHECK(text->length == 6 && memcmp(text->text, KANA_UTF8, 6) == 0 &&
                  preedit->data.preedit.length == 0,
              "the result as text, and the composition ends");
        CHECK(Find(program, mwin_eventTextInput, 1) == nullptr &&
                  Find(program, mwin_eventInputStateReset, 0) == nullptr,
              "the result once, and the text state kept");
        CHECK(Compose(program), "compose again");
        break;
    }
    case phaseRecompose:
        CHECK(Composing(&preedit->data.preedit), "composing again");
        CHECK(mwinRequestTextInput(context, program->window, false, caret, nullptr) == mwin_success,
              "stop accepting text");
        break;
    default:
        CHECK(preedit->data.preedit.length == 0 &&
                  Find(program, mwin_eventTextInput, 0) == nullptr &&
                  ImmGetContext(program->hwnd) == nullptr,
              "stopping cancels the composition and takes the context away");
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startMs = GetTickCount64();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){640.0f, 480.0f};
    program->startMs = GetTickCount64();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (Ready(program) || program->phase == phaseEnable)
    {
        Advance(program, context);
    }
    else if (GetTickCount64() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
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
    Program program = {0};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on Windows");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    return s_failures == 0 && program.skipped ? 77 : (s_failures == 0 ? 0 : 1);
}
