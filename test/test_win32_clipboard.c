// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's clipboard, against Windows' own: text written
// lands as CF_UNICODETEXT, the window its owner; text another program
// put there is read, a lone surrogate replaced; a clipboard without text
// reads as empty; text past the limit is too large. Data (mwin-0029)
// lands as registered formats, image/png as "PNG", beside its text, and
// alone without text, an empty item among them; another program's PNG
// is read; a type the clipboard lacks fails; the primary selection is
// unsupported. While another thread holds the clipboard open, reads and
// writes fail.

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

// The last read asked, whose payload Found and FoundData copy out.
static mwinRequestId s_read;

static int Read(mwinContext* context, mwinWindowId window)
{
    s_read = (mwinRequestId){0};
    return mwinRequestClipboardRead(context, window, &s_read) == mwin_success
               ? Outcome(context, s_read)
               : -1;
}

static bool Found(mwinContext* context, const char* expected)
{
    char text[LIMIT];
    size_t length = 0;
    return mwinGetClipboardText(context, s_read, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static const uint8_t s_png[] = {0x89, 'P', 'N', 'G', 0, '\r', '\n'};

// Whether the clipboard holds bytes as a format, at least as many as the
// memory holds.
static bool HoldsData(UINT format, const uint8_t* expected, size_t length)
{
    bool same = false;
    if (OpenClipboard(nullptr))
    {
        HANDLE memory = GetClipboardData(format);
        const uint8_t* bytes = memory != nullptr ? GlobalLock(memory) : nullptr;
        same = bytes != nullptr && GlobalSize(memory) >= length &&
               memcmp(bytes, expected, length) == 0;
        if (bytes != nullptr)
        {
            GlobalUnlock(memory);
        }
        CloseClipboard();
    }
    return same;
}

static bool FoundData(mwinContext* context, const uint8_t* expected, size_t length)
{
    uint8_t data[LIMIT];
    size_t found = 0;
    return mwinGetClipboardData(context, s_read, data, sizeof(data), &found) == mwin_success &&
           found >= length && memcmp(data, expected, length) == 0;
}

static int ReadData(mwinContext* context, mwinWindowId window, const char* mime)
{
    s_read = (mwinRequestId){0};
    return mwinRequestClipboardReadData(context, window, mime, strlen(mime), &s_read) ==
                   mwin_success
               ? Outcome(context, s_read)
               : -1;
}

// Holds the clipboard open on its own thread until told to let go.
static DWORD WINAPI Hold(void* data)
{
    HANDLE* events = data;
    bool held = OpenClipboard(nullptr);
    SetEvent(events[0]);
    if (held)
    {
        WaitForSingleObject(events[1], INFINITE);
        CloseClipboard();
    }
    return held ? 0 : 1;
}

static void CheckHeld(mwinContext* context, mwinWindowId window)
{
    HANDLE events[2] = {CreateEventW(nullptr, TRUE, FALSE, nullptr),
                        CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE thread = CreateThread(nullptr, 0, Hold, events, 0, nullptr);
    WaitForSingleObject(events[0], INFINITE);
    mwinRequestId request = {0};
    CHECK(Read(context, window) == mwin_outcomeFailed &&
              ReadData(context, window, "image/png") == mwin_outcomeFailed &&
              mwinRequestClipboardWrite(context, window, "x", 1, &request) == mwin_success &&
              Outcome(context, request) == mwin_outcomeFailed,
          "while another thread holds the clipboard, reads and writes fail");
    SetEvent(events[1]);
    DWORD held = 1;
    WaitForSingleObject(thread, INFINITE);
    (void)GetExitCodeThread(thread, &held);
    CHECK(held == 0, "the other thread held the clipboard");
    CloseHandle(thread);
    CloseHandle(events[0]);
    CloseHandle(events[1]);
}

static void CheckData(mwinContext* context, mwinWindowId window)
{
    static const uint8_t other[] = {'o', 't', 'h', 'e', 'r'};
    UINT png = RegisterClipboardFormatW(L"PNG");
    UINT maul = RegisterClipboardFormatW(L"application/x-maul");
    mwinClipboardItem items[] = {
        {"image/png", 9, s_png, sizeof(s_png)},
        {"text/plain", 10, "hi", 2},
        {"application/x-maul", 18, other, sizeof(other)},
    };
    mwinRequestId request = {0};
    CHECK(mwinRequestClipboardWriteData(context, window, items, 3, &request) == mwin_success &&
              Outcome(context, request) == mwin_outcomeDone && Holds(L"hi") &&
              HoldsData(png, s_png, sizeof(s_png)) && HoldsData(maul, other, sizeof(other)),
          "data written as registered formats, image/png as PNG, beside its text");
    CHECK(mwinRequestClipboardWriteData(context, window, items, 1, &request) == mwin_success &&
              Outcome(context, request) == mwin_outcomeDone &&
              HoldsData(png, s_png, sizeof(s_png)) && !IsClipboardFormatAvailable(CF_UNICODETEXT),
          "data alone without text");
    mwinClipboardItem empty[] = {items[0], {"application/x-empty", 19, "", 0}};
    CHECK(mwinRequestClipboardWriteData(context, window, empty, 2, &request) == mwin_success &&
              Outcome(context, request) == mwin_outcomeDone &&
              IsClipboardFormatAvailable(RegisterClipboardFormatW(L"application/x-empty")),
          "an empty item written too");
    CHECK(OpenClipboard(nullptr) && EmptyClipboard(), "open the clipboard");
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(other));
    memcpy(GlobalLock(memory), other, sizeof(other));
    GlobalUnlock(memory);
    CHECK(SetClipboardData(png, memory) != nullptr, "set the clipboard");
    CloseClipboard();
    CHECK(ReadData(context, window, "image/png") == mwin_outcomeDone &&
              FoundData(context, other, sizeof(other)),
          "another program's PNG read");
    CHECK(ReadData(context, window, "image/gif") == mwin_outcomeFailed,
          "a type the clipboard lacks fails");
    CHECK(mwinRequestPrimaryRead(context, window, &request) == mwin_success &&
              Outcome(context, request) == mwin_outcomeUnsupported,
          "no primary selection");
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
    CheckData(context, window);
    CheckHeld(context, window);
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
