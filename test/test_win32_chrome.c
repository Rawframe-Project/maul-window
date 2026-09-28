// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's custom chrome, read back from Windows:
// - The client area is the whole window, which keeps a caption's
//   styles.
// - Hit regions become Windows' hit codes: caption, edges, a maximize
//   button, and the client for the other buttons.
// - A maximize button's frame messages reach the program as the client
//   area's would, and do not maximize the window.
// - A leave from the client area into a hit region is none, and a leave
//   from the window is one.
// - Maximized, the client area stays on the monitor.
// - Without resizing, edges are borders and the maximize button is the
//   client's.
// - Back to a decorated window, the frame comes back around the same
//   client area.
// - Snap layouts are named on Windows 11 alone.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/system.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 10000u

typedef struct Program
{
    ULONGLONG startMs;
    mwinWindowId window;
    mwinRequestId request;
    int step;
    // What the last messages brought: the event types seen, as bits, and
    // the last pointer record.
    uint64_t seen;
    mwinPointerEvent pointer;
    bool done;
} Program;

// A caption across the top with maximize and close buttons at its right
// end, a left edge and a bottom right corner, on a window of 400 by 300.
static const mwinHitRegion s_regions[5] = {
    {{0.0f, 0.0f, 400.0f, 30.0f}, mwin_hitCaption},
    {{340.0f, 0.0f, 30.0f, 30.0f}, mwin_hitMaximize},
    {{370.0f, 0.0f, 30.0f, 30.0f}, mwin_hitClose},
    {{0.0f, 30.0f, 4.0f, 270.0f}, mwin_hitLeft},
    {{392.0f, 292.0f, 8.0f, 8.0f}, mwin_hitBottomRight},
};

static int Outcome(mwinContext* context, Program* program)
{
    mwinEvent event;
    int outcome = -1;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->seen |= 1ull << event.type;
        if (event.type == mwin_eventCursorMoved || event.type == mwin_eventButtonDown ||
            event.type == mwin_eventButtonUp)
        {
            program->pointer = event.data.pointer;
        }
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == program->request.index1 &&
            completion->request.generation == program->request.generation)
        {
            outcome = completion->outcome;
        }
    }
    return outcome;
}

static HWND Handle(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (HWND)handles.handles.win32.hwnd
               : nullptr;
}

// A point of the client area in logical units, on the desktop, as a
// message carries it.
static LPARAM Screen(HWND hwnd, float x, float y)
{
    float scale = (float)GetDpiForWindow(hwnd) / 96.0f;
    POINT point = {(LONG)(x * scale), (LONG)(y * scale)};
    ClientToScreen(hwnd, &point);
    return MAKELPARAM((WORD)(int16_t)point.x, (WORD)(int16_t)point.y);
}

static LRESULT Hit(HWND hwnd, float x, float y)
{
    return SendMessageW(hwnd, WM_NCHITTEST, 0, Screen(hwnd, x, y));
}

static bool Whole(HWND hwnd)
{
    RECT client;
    RECT frame;
    GetClientRect(hwnd, &client);
    GetWindowRect(hwnd, &frame);
    float scale = (float)GetDpiForWindow(hwnd) / 96.0f;
    return client.right == frame.right - frame.left && client.bottom == frame.bottom - frame.top &&
           client.right == (LONG)(400.0f * scale + 0.5f);
}

static bool SnapLayouts(void)
{
    LONG(WINAPI * get)(OSVERSIONINFOW*) = nullptr;
    FARPROC found = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    memcpy((void*)&get, (const void*)&found, sizeof(get));
    OSVERSIONINFOW version = {.dwOSVersionInfoSize = sizeof(version)};
    return get != nullptr && get(&version) == 0 && version.dwBuildNumber >= 22000;
}

static void CheckHits(HWND hwnd)
{
    CHECK(Hit(hwnd, 100.0f, 10.0f) == HTCAPTION && Hit(hwnd, 350.0f, 10.0f) == HTMAXBUTTON &&
              Hit(hwnd, 380.0f, 10.0f) == HTCLIENT && Hit(hwnd, 2.0f, 100.0f) == HTLEFT &&
              Hit(hwnd, 396.0f, 296.0f) == HTBOTTOMRIGHT && Hit(hwnd, 200.0f, 150.0f) == HTCLIENT,
          "hit regions as Windows' hit codes");
}

// The maximize button's frame messages, and leaves.
static void CheckButton(mwinContext* context, Program* program, HWND hwnd)
{
    program->seen = 0;
    SendMessageW(hwnd, WM_NCMOUSEMOVE, HTMAXBUTTON, Screen(hwnd, 355.0f, 15.0f));
    (void)Outcome(context, program);
    CHECK((program->seen & (1ull << mwin_eventCursorMoved)) != 0 &&
              program->pointer.position.x == 355.0f && program->pointer.position.y == 15.0f,
          "the pointer over a maximize button moves as over the client area");
    program->seen = 0;
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTMAXBUTTON, Screen(hwnd, 355.0f, 15.0f));
    (void)Outcome(context, program);
    bool pressed = (program->seen & (1ull << mwin_eventButtonDown)) != 0 &&
                   program->pointer.button == mwin_buttonLeft;
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    (void)Outcome(context, program);
    program->seen = 0;
    SendMessageW(hwnd, WM_NCLBUTTONDBLCLK, HTMAXBUTTON, Screen(hwnd, 355.0f, 15.0f));
    (void)Outcome(context, program);
    bool again =
        (program->seen & (1ull << mwin_eventButtonDown)) != 0 && program->pointer.clicks == 2;
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    CHECK(pressed && again && !IsZoomed(hwnd),
          "presses of a maximize button, the second a double click, the program's, not Windows'");
    program->seen = 0;
    POINT inside = {0, 0};
    ClientToScreen(hwnd, &inside);
    SetCursorPos(inside.x + 20, inside.y + 50);
    SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
    SendMessageW(hwnd, WM_NCMOUSELEAVE, 0, 0);
    (void)Outcome(context, program);
    bool stayed = (program->seen & (1ull << mwin_eventCursorLeft)) == 0;
    RECT frame;
    GetWindowRect(hwnd, &frame);
    SetCursorPos(frame.right + 50, frame.bottom + 50);
    SendMessageW(hwnd, WM_NCMOUSELEAVE, 0, 0);
    (void)Outcome(context, program);
    CHECK(stayed && (program->seen & (1ull << mwin_eventCursorLeft)) != 0,
          "a leave into a hit region none, a leave from the window one");
}

static void Ask(Program* program, mwinResult status)
{
    CHECK(status == mwin_success, "ask");
    program->step += 1;
}

static void Next(mwinContext* context, Program* program)
{
    HWND hwnd = Handle(context, program->window);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    mwinSystemFacts facts;
    switch (program->step)
    {
    case 0:
        CHECK(Whole(hwnd) && (style & (WS_CAPTION | WS_THICKFRAME)) == (WS_CAPTION | WS_THICKFRAME),
              "the client area the whole window, which keeps a caption's styles");
        CHECK(mwinGetSystemFacts(context, &facts) == mwin_success &&
                  facts.snapLayouts == SnapLayouts(),
              "snap layouts named on Windows 11 alone");
        Ask(program,
            mwinRequestHitRegions(context, program->window, s_regions, 5, &program->request));
        break;
    case 1:
        CheckHits(hwnd);
        CheckButton(context, program, hwnd);
        Ask(program,
            mwinRequestMode(context, program->window, mwin_modeMaximized, &program->request));
        break;
    case 2:
    {
        MONITORINFO monitor = {.cbSize = sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        RECT client;
        GetClientRect(hwnd, &client);
        POINT origin = {0, 0};
        ClientToScreen(hwnd, &origin);
        CHECK(IsZoomed(hwnd) && origin.x >= monitor.rcWork.left && origin.y >= monitor.rcWork.top &&
                  origin.x + client.right <= monitor.rcWork.right &&
                  origin.y + client.bottom <= monitor.rcWork.bottom,
              "maximized, the client area on the monitor");
        Ask(program,
            mwinRequestMode(context, program->window, mwin_modeWindowed, &program->request));
        break;
    }
    case 3:
        Ask(program,
            mwinRequestStyle(context, program->window, mwin_styleCustomChrome, &program->request));
        break;
    case 4:
        CHECK(Whole(hwnd) && Hit(hwnd, 2.0f, 100.0f) == HTBORDER &&
                  Hit(hwnd, 396.0f, 296.0f) == HTBORDER && Hit(hwnd, 350.0f, 10.0f) == HTCLIENT,
              "without resizing, edges borders and the maximize button the client's");
        Ask(program,
            mwinRequestStyle(context, program->window, mwin_styleDecorated | mwin_styleResizable,
                             &program->request));
        break;
    default:
    {
        RECT client;
        GetClientRect(hwnd, &client);
        float scale = (float)GetDpiForWindow(hwnd) / 96.0f;
        CHECK(!Whole(hwnd) && client.right == (LONG)(400.0f * scale + 0.5f) &&
                  client.bottom == (LONG)(300.0f * scale + 0.5f),
              "decorated again, the frame around the same client area");
        program->done = true;
        break;
    }
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){400.0f, 300.0f};
    def.style = mwin_styleResizable | mwin_styleCustomChrome;
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    int outcome = program->done ? -1 : Outcome(context, program);
    CHECK(outcome < 0 || outcome == mwin_outcomeDone, "every request done");
    if (outcome >= 0)
    {
        Next(context, program);
    }
    bool late = GetTickCount64() - program->startMs > DEADLINE_MS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
