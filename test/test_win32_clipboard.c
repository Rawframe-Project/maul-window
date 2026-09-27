// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's clipboard, against Windows' own: text written
// lands as CF_UNICODETEXT, the window its owner; text another program put there is read,
// a lone surrogate replaced; a clipboard without text reads as empty;
// text past the limit is too large.

#include "test_harness.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 10000u
#define LIMIT       32

typedef struct Program
{
    ULONGLONG startMs;
    mwinWindowId window;
    mwinRequestId create;
    bool done;
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

// Puts UTF-16 on the clipboard as another program would; none empties
// it.
static void Put(const wchar_t* units, size_t count)
{
    CHECK(OpenClipboard(nullptr) && EmptyClipboard(), "open the clipboard");
    if (count > 0)
    {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, count * sizeof(wchar_t));
        wchar_t* copy = GlobalLock(memory);
        memcpy(copy, units, count * sizeof(wchar_t));
        GlobalUnlock(memory);
        CHECK(SetClipboardData(CF_UNICODETEXT, memory) != nullptr, "set the clipboard");
    }
    CloseClipboard();
}

// Whether the clipboard holds this UTF-16 text.
static bool Holds(const wchar_t* expected)
{
    bool same = false;
    if (OpenClipboard(nullptr))
    {
        HANDLE memory = GetClipboardData(CF_UNICODETEXT);
        const wchar_t* units = memory != nullptr ? GlobalLock(memory) : nullptr;
        same = units != nullptr && wcscmp(units, expected) == 0;
        if (units != nullptr)
        {
            GlobalUnlock(memory);
        }
        CloseClipboard();
    }
    return same;
}

static int Read(mwinContext* context, mwinWindowId window)
{
    mwinRequestId request = {0};
    return mwinRequestClipboardRead(context, window, &request) == mwin_success
               ? Outcome(context, request)
               : -1;
}

static bool Found(mwinContext* context, const char* expected)
{
    char text[LIMIT];
    size_t length = 0;
    return mwinGetClipboardText(context, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static void Check(mwinContext* context, mwinWindowId window)
{
    mwinRequestId request = {0};
    mwinNativeHandles handles;
    CHECK(mwinGetNativeHandles(context, window, &handles) == mwin_success, "the handles");
    CHECK(mwinRequestClipboardWrite(context, window, "h\xC3\xA9llo \xF0\x9F\x98\x80", 11,
                                    &request) == mwin_success &&
              Outcome(context, request) == mwin_outcomeDone && Holds(L"h\x00E9llo \xD83D\xDE00") &&
              GetClipboardOwner() == (HWND)handles.handles.win32.hwnd,
          "written text on the clipboard as UTF-16, the window its owner");
    static const wchar_t lone[] = {L'A', 0xD800, L'B', 0};
    Put(lone, 4);
    CHECK(Read(context, window) == mwin_outcomeDone && Found(context, "A\xEF\xBF\xBD"
                                                                      "B"),
          "another program's text read, a lone surrogate replaced");
    Put(nullptr, 0);
    CHECK(Read(context, window) == mwin_outcomeDone && Found(context, ""),
          "a clipboard without text read as empty");
    wchar_t large[LIMIT + 2];
    wmemset(large, L'x', LIMIT + 1);
    large[LIMIT + 1] = 0;
    Put(large, LIMIT + 2);
    CHECK(Read(context, window) == mwin_outcomeTooLarge, "text past the limit too large");
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    return mwinCreateWindow(context, &def, &program->window, &program->create);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    if (Outcome(context, program->create) == mwin_outcomeDone)
    {
        Check(context, program->window);
        program->done = true;
        return mwin_frameStop;
    }
    return GetTickCount64() - program->startMs > DEADLINE_MS ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.clipboardBytes = LIMIT;
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
