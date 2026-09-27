// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's services, against Windows' own: the display kept
// awake while a window that asks shows, let go while it is minimized,
// when it no longer asks and at the end, read back from the thread's
// execution state; a file that does not exist not revealed, and one
// that does shown (not under wine, whose Explorer would stay open); an
// address opened by the handler of its scheme, with this program
// standing in for the browser (under wine only: Windows takes the
// user's choice of browser over the registry).

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/services.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// After windows.h, whose types it takes.
#include <shellapi.h>

#define DEADLINE_MS 20000u
#define AWAKE       (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)
#define HANDLER     L"http\\shell\\open\\command"

// An address with a path outside ASCII, so it crosses UTF-16 whole.
static const char s_address[] = "http://example.com/\xC3\xA9?a=1";

typedef enum Phase
{
    phaseCreate,
    phaseAwake,
    phaseMinimized,
    phaseRestored,
    phaseAsleep,
    phaseOpened,
    phaseEnding,
    phaseDone,
} Phase;

typedef struct Program
{
    ULONGLONG startMs;
    mwinWindowId window;
    mwinRequestId request;
    Phase phase;
    bool wine;
    WCHAR opened[MAX_PATH];
    WCHAR handler[2 * MAX_PATH];
    DWORD handlerBytes;
} Program;

// The outcome of a request's completion, drained from the stream, or -1.
static int Outcome(mwinContext* context, mwinRequestId request)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == request.index1 &&
            completion->request.generation == request.generation)
        {
            return completion->outcome;
        }
    }
    return -1;
}

// The thread's execution state, read by setting what it should be:
// Windows answers with the state before.
static bool Holds(EXECUTION_STATE expected)
{
    return (SetThreadExecutionState(expected) & AWAKE) == expected;
}

static mwinWindowState StateOf(mwinContext* context, mwinWindowId window)
{
    mwinWindowState state = {0};
    CHECK(mwinGetWindowState(context, window, &state) == mwin_success, "the window's state");
    return state;
}

static int Reveal(mwinContext* context, mwinWindowId window, const char* path)
{
    mwinRequestId request = {0};
    return mwinRequestRevealFile(context, window, path, strlen(path), &request) == mwin_success
               ? Outcome(context, request)
               : -1;
}

static void CheckReveal(mwinContext* context, const Program* program)
{
    CHECK(Reveal(context, program->window, "C:/mwin-missing/x.txt") == mwin_outcomeFailed,
          "a file that does not exist not revealed");
    if (program->wine)
    {
        return;
    }
    // This program, with the separators a program on any platform
    // writes.
    WCHAR wide[MAX_PATH];
    char path[3 * MAX_PATH];
    DWORD units = GetModuleFileNameW(nullptr, wide, MAX_PATH);
    int bytes =
        WideCharToMultiByte(CP_UTF8, 0, wide, (int)units, path, sizeof(path) - 1, nullptr, nullptr);
    path[bytes] = '\0';
    for (char* at = path; *at != '\0'; at++)
    {
        *at = *at == '\\' ? '/' : *at;
    }
    CHECK(Reveal(context, program->window, path) == mwin_outcomeDone, "a file shown in Explorer");
}

// Points the http handler at this program, keeping the one there.
static bool Handle(Program* program)
{
    WCHAR self[MAX_PATH];
    WCHAR command[2 * MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    (void)swprintf(command, 2 * MAX_PATH, L"\"%ls\" --opened \"%%1\"", self);
    program->handlerBytes = sizeof(program->handler);
    if (RegGetValueW(HKEY_CLASSES_ROOT, HANDLER, nullptr, RRF_RT_REG_SZ, nullptr, program->handler,
                     &program->handlerBytes) != ERROR_SUCCESS)
    {
        program->handlerBytes = 0;
    }
    DWORD size = (DWORD)((wcslen(command) + 1) * sizeof(WCHAR));
    return RegSetKeyValueW(HKEY_CLASSES_ROOT, HANDLER, nullptr, REG_SZ, command, size) ==
           ERROR_SUCCESS;
}

static void Unhandle(const Program* program)
{
    if (program->handlerBytes > 0)
    {
        (void)RegSetKeyValueW(HKEY_CLASSES_ROOT, HANDLER, nullptr, REG_SZ, program->handler,
                              program->handlerBytes);
    }
}

// The address the stand-in browser was given, once it wrote it.
static bool Opened(const Program* program)
{
    char text[128];
    FILE* file = _wfopen(program->opened, L"rb");
    size_t length = file != nullptr ? fread(text, 1, sizeof(text), file) : 0;
    if (file != nullptr)
    {
        fclose(file);
    }
    return length == sizeof(s_address) - 1 && memcmp(text, s_address, length) == 0;
}

// Keeping awake, followed through the window's minimizing.
static void StepAwake(mwinContext* context, Program* program)
{
    mwinWindowState state = StateOf(context, program->window);
    switch (program->phase)
    {
    case phaseAwake:
        CHECK(state.awake && Holds(AWAKE), "the display kept awake while the window shows");
        CHECK(mwinRequestMode(context, program->window, mwin_modeMinimized, nullptr) ==
                  mwin_success,
              "minimize");
        program->phase = phaseMinimized;
        break;
    case phaseMinimized:
        if (state.mode == mwin_modeMinimized)
        {
            CHECK(Holds(ES_CONTINUOUS), "the display let go while the window is minimized");
            CHECK(mwinRequestMode(context, program->window, mwin_modeWindowed, nullptr) ==
                      mwin_success,
                  "restore");
            program->phase = phaseRestored;
        }
        break;
    case phaseRestored:
        if (state.mode == mwin_modeWindowed)
        {
            CHECK(Holds(AWAKE), "the display kept awake again once the window shows");
            CHECK(mwinRequestKeepAwake(context, program->window, false, &program->request) ==
                      mwin_success,
                  "let the display go");
            program->phase = phaseAsleep;
        }
        break;
    default:
        break;
    }
}

// Keeps the display awake again, for the end to let go.
static void Awaken(mwinContext* context, Program* program)
{
    CHECK(mwinRequestKeepAwake(context, program->window, true, nullptr) == mwin_success,
          "keep the display awake to the end");
    program->phase = phaseEnding;
}

// The address, handed to the stand-in browser.
static void StepOpen(mwinContext* context, Program* program)
{
    if (program->phase == phaseAsleep)
    {
        CHECK(Outcome(context, program->request) == mwin_outcomeDone &&
                  !StateOf(context, program->window).awake && Holds(ES_CONTINUOUS),
              "the display let go when the window no longer asks");
        CheckReveal(context, program);
        if (!program->wine)
        {
            Awaken(context, program);
            return;
        }
        CHECK(Handle(program), "this program handles http");
        mwinRequestId request = {0};
        CHECK(mwinRequestOpenUrl(context, program->window, s_address, sizeof(s_address) - 1,
                                 &request) == mwin_success &&
                  Outcome(context, request) == mwin_outcomeDone,
              "the address opened");
        program->phase = phaseOpened;
    }
    else if (program->phase == phaseOpened && Opened(program))
    {
        Awaken(context, program);
    }
    else if (program->phase == phaseEnding)
    {
        CHECK(Holds(AWAKE), "the display kept awake as the program ends");
        program->phase = phaseDone;
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    if (program->phase == phaseCreate)
    {
        if (Outcome(context, program->request) == mwin_outcomeDone)
        {
            mwinRequestId request = {0};
            CHECK(mwinRequestKeepAwake(context, program->window, true, &request) == mwin_success &&
                      Outcome(context, request) == mwin_outcomeDone,
                  "keep the display awake");
            program->phase = phaseAwake;
        }
    }
    else if (program->phase < phaseAsleep)
    {
        StepAwake(context, program);
    }
    else
    {
        StepOpen(context, program);
    }
    bool late = GetTickCount64() - program->startMs > DEADLINE_MS;
    return program->phase == phaseDone || late ? mwin_frameStop : mwin_frameContinue;
}

// Stands in for the browser: writes the address it was given in UTF-8.
static int Browse(const WCHAR* address, const WCHAR* into)
{
    char text[128];
    int length = WideCharToMultiByte(CP_UTF8, 0, address, -1, text, sizeof(text), nullptr, nullptr);
    FILE* file = _wfopen(into, L"wb");
    if (file == nullptr || length <= 0)
    {
        return 1;
    }
    fwrite(text, 1, (size_t)length - 1, file);
    fclose(file);
    return 0;
}

int main(void)
{
    static Program program;
    WCHAR folder[MAX_PATH];
    GetTempPathW(MAX_PATH, folder);
    (void)swprintf(program.opened, MAX_PATH, L"%lsmwin-opened.txt", folder);
    int count = 0;
    WCHAR** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (count == 3 && wcscmp(arguments[1], L"--opened") == 0)
    {
        return Browse(arguments[2], program.opened);
    }
    LocalFree((HLOCAL)arguments);
    (void)DeleteFileW(program.opened);
    program.wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    mwinResult result = mwinRun(&def);
    if (program.wine)
    {
        Unhandle(&program);
    }
    CHECK(result == mwin_success && program.phase == phaseDone, "the program runs to its end");
    // The end lets the display go though the program never said.
    CHECK(Holds(ES_CONTINUOUS), "the display let go at the end");
    return s_failures == 0 ? 0 : 1;
}
