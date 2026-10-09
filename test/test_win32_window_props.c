// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's window properties, read back from Windows: a
// decorated, resizable window always on top has a caption, system menu,
// minimize and maximize boxes and a sizing frame, and is topmost; a
// plain one is a popup without them, not topmost; the title outside
// ASCII. Size limits bound the frame Windows tracks to the client sizes
// asked for, a zero side left free; an aspect ratio keeps the client
// area to it as each edge and corner is dragged, and lifted, leaves the
// drag as it is; an opacity below 1 makes the window layered at that
// alpha, and 1 takes the layering away.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <math.h>
#include <string.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 20000u
// What WM_GETMINMAXINFO holds before the window bounds it.
#define FREE_MIN 1
#define FREE_MAX 9999

typedef enum Phase
{
    phaseCreate,
    phaseSet,
    phaseLifted,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    ULONGLONG startMs;
    mwinWindowId framed;
    mwinWindowId plain;
    int completions;
    bool timedOut;
} Program;

static HWND Handle(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (HWND)handles.handles.win32.hwnd
               : nullptr;
}

// The windows made and the requests done, drained: every one done.
static int Completed(mwinContext* context)
{
    int done = 0;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventRequestCompleted)
        {
            CHECK(event.data.completion.outcome == mwin_outcomeDone, "a request done");
            done += 1;
        }
        done += event.type == mwin_eventWindowCreated;
    }
    return done;
}

// The frame of a client size in pixels, as Windows sizes the window's.
static SIZE FrameOf(HWND hwnd, LONG width, LONG height)
{
    RECT rect = {0, 0, width, height};
    AdjustWindowRectExForDpi(&rect, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE, 0,
                             GetDpiForWindow(hwnd));
    return (SIZE){rect.right - rect.left, rect.bottom - rect.top};
}

static LONG Pixels(HWND hwnd, float size)
{
    return (LONG)lroundf(size * (float)GetDpiForWindow(hwnd) / 96.0f);
}

static MINMAXINFO Bounds(HWND hwnd)
{
    MINMAXINFO info = {.ptMinTrackSize = {FREE_MIN, FREE_MIN},
                       .ptMaxTrackSize = {FREE_MAX, FREE_MAX}};
    SendMessageW(hwnd, WM_GETMINMAXINFO, 0, (LPARAM)&info);
    return info;
}

// The client size after the user drags an edge or corner to a frame.
static SIZE Dragged(HWND hwnd, WPARAM edge, LONG width, LONG height)
{
    SIZE frame = FrameOf(hwnd, 0, 0);
    RECT rect = {100, 100, 100 + frame.cx + width, 100 + frame.cy + height};
    SendMessageW(hwnd, WM_SIZING, edge, (LPARAM)&rect);
    return (SIZE){rect.right - rect.left - frame.cx, rect.bottom - rect.top - frame.cy};
}

static void CheckCreated(mwinContext* context, const Program* program)
{
    HWND framed = Handle(context, program->framed);
    HWND plain = Handle(context, program->plain);
    DWORD wanted = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    CHECK(((DWORD)GetWindowLongPtrW(framed, GWL_STYLE) & wanted) == wanted &&
              (GetWindowLongPtrW(framed, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
          "decorated, resizable, always on top");
    DWORD style = (DWORD)GetWindowLongPtrW(plain, GWL_STYLE);
    CHECK((style & WS_POPUP) != 0 && (style & (WS_CAPTION | WS_THICKFRAME)) == 0 &&
              (GetWindowLongPtrW(plain, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
          "a plain popup, not on top");
    WCHAR title[16];
    CHECK(GetWindowTextW(framed, title, 16) == 7 && wcscmp(title, L"H\x00E9llo \x2603") == 0,
          "the title outside ASCII");
}

static void CheckSet(mwinContext* context, const Program* program)
{
    HWND hwnd = Handle(context, program->framed);
    SIZE least = FrameOf(hwnd, Pixels(hwnd, 200.0f), Pixels(hwnd, 100.0f));
    SIZE most = FrameOf(hwnd, Pixels(hwnd, 400.0f), Pixels(hwnd, 300.0f));
    MINMAXINFO bounds = Bounds(hwnd);
    CHECK(bounds.ptMinTrackSize.x == least.cx && bounds.ptMinTrackSize.y == least.cy &&
              bounds.ptMaxTrackSize.x == most.cx && bounds.ptMaxTrackSize.y == most.cy,
          "the frame bounded to the client sizes asked for");
    SIZE right = Dragged(hwnd, WMSZ_RIGHT, 400, 50);
    SIZE corner = Dragged(hwnd, WMSZ_BOTTOMRIGHT, 300, 50);
    SIZE top = Dragged(hwnd, WMSZ_TOP, 50, 100);
    SIZE topLeft = Dragged(hwnd, WMSZ_TOPLEFT, 240, 50);
    SIZE bottom = Dragged(hwnd, WMSZ_BOTTOM, 50, 80);
    CHECK(right.cx == 400 && right.cy == 200 && corner.cx == 300 && corner.cy == 150 &&
              top.cx == 200 && top.cy == 100 && topLeft.cx == 240 && topLeft.cy == 120 &&
              bottom.cx == 160 && bottom.cy == 80,
          "the client area kept to 2:1 as each edge and corner is dragged");
    BYTE alpha = 0;
    DWORD flags = 0;
    CHECK((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0 &&
              GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) && alpha == 128 &&
              (flags & LWA_ALPHA) != 0,
          "half opaque: layered at alpha 128");
}

static void CheckLifted(mwinContext* context, const Program* program)
{
    HWND hwnd = Handle(context, program->framed);
    MINMAXINFO bounds = Bounds(hwnd);
    SIZE least = FrameOf(hwnd, 0, Pixels(hwnd, 100.0f));
    SIZE most = FrameOf(hwnd, 0, Pixels(hwnd, 300.0f));
    CHECK(bounds.ptMinTrackSize.x == FREE_MIN && bounds.ptMinTrackSize.y == least.cy &&
              bounds.ptMaxTrackSize.x == FREE_MAX && bounds.ptMaxTrackSize.y == most.cy,
          "a zero side left free");
    SIZE right = Dragged(hwnd, WMSZ_RIGHT, 400, 50);
    CHECK(right.cx == 400 && right.cy == 50, "the ratio lifted: a drag as it is");
    CHECK((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0,
          "fully opaque: no longer layered");
}

// Asks for the phase's limits, ratio and opacity: three requests.
static void Ask(mwinContext* context, const Program* program, bool set)
{
    mwinSize least = set ? (mwinSize){200.0f, 100.0f} : (mwinSize){0.0f, 100.0f};
    mwinSize most = set ? (mwinSize){400.0f, 300.0f} : (mwinSize){0.0f, 300.0f};
    CHECK(mwinRequestSizeLimits(context, program->framed, least, most, nullptr) == mwin_success &&
              mwinRequestAspectRatio(context, program->framed, set ? 2 : 0, set ? 1 : 0, nullptr) ==
                  mwin_success &&
              mwinRequestOpacity(context, program->framed, set ? 0.5f : 1.0f, nullptr) ==
                  mwin_success,
          "the limits, the ratio and the opacity asked");
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    def.title = "H\xC3\xA9llo \xE2\x98\x83";
    def.titleLength = 10;
    def.style = mwin_styleDecorated | mwin_styleResizable | mwin_styleAlwaysOnTop;
    mwinResult made = mwinCreateWindow(context, &def, &program->framed, nullptr);
    def.title = "Plain";
    def.titleLength = 5;
    def.style = 0;
    return made == mwin_success ? mwinCreateWindow(context, &def, &program->plain, nullptr) : made;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    program->completions += Completed(context);
    // Two windows made, then three requests a phase.
    int needed = program->phase == phaseCreate ? 2 : 3;
    if (program->completions >= needed && program->phase != phaseDone)
    {
        program->completions = 0;
        switch (program->phase)
        {
        case phaseCreate:
            CheckCreated(context, program);
            Ask(context, program, true);
            break;
        case phaseSet:
            CheckSet(context, program);
            Ask(context, program, false);
            break;
        default:
            CheckLifted(context, program);
            break;
        }
        program->phase += 1;
        program->startMs = GetTickCount64();
    }
    if (GetTickCount64() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && !program.timedOut && program.phase == phaseDone,
          "every phase ran in time");
    return s_failures == 0 ? 0 : 1;
}
