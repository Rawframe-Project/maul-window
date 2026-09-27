// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's owned and popup windows, read back from Windows:
// an owned window owned by its owner's and off the taskbar; a menu an
// owned undecorated tool window at its place against its owner's client
// area, following its owner, placed again against it, and asked to
// close when the keyboard goes elsewhere; a tooltip never activated;
// and all gone with their owner.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 10000u
#define STEP_MS     3000u

typedef struct Program
{
    ULONGLONG startMs;
    ULONGLONG stepMs;
    int step;
    mwinWindowId owner;
    mwinWindowId dialog;
    mwinWindowId menu;
    mwinWindowId tooltip;
    HWND hwnds[4];
    int created;
    int menuMoves;
    mwinPosition menuPlace;
    bool menuClose;
    bool done;
} Program;

static bool Same(mwinWindowId a, mwinWindowId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void Collect(mwinContext* context, Program* program)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventRequestCompleted &&
            event.data.completion.outcome == mwin_outcomeDone)
        {
            program->created += 1;
        }
        if (event.type == mwin_eventMoved && Same(event.window, program->menu))
        {
            program->menuMoves += 1;
            program->menuPlace = event.data.position;
        }
        program->menuClose = program->menuClose || (event.type == mwin_eventCloseRequested &&
                                                    Same(event.window, program->menu));
    }
}

static HWND Handle(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (HWND)handles.handles.win32.hwnd
               : nullptr;
}

static POINT Origin(HWND hwnd)
{
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin);
    return origin;
}

// Whether the menu's client area is at an offset in logical units from
// its owner's.
static bool Placed(const Program* program, float x, float y)
{
    float scale = (float)GetDpiForWindow(program->hwnds[0]) / 96.0f;
    POINT owner = Origin(program->hwnds[0]);
    POINT menu = Origin(program->hwnds[2]);
    return menu.x - owner.x == (LONG)(x * scale + 0.5f) &&
           menu.y - owner.y == (LONG)(y * scale + 0.5f);
}

static mwinWindowId Create(mwinContext* context, mwinWindowId owner, mwinWindowKind kind,
                           mwinPosition position)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){160.0f, 90.0f};
    def.owner = owner;
    def.kind = kind;
    def.position = position;
    mwinWindowId window = {0};
    CHECK(mwinCreateWindow(context, &def, &window, nullptr) == mwin_success, "create");
    return window;
}

static void CheckMade(const Program* program)
{
    LONG_PTR dialog = GetWindowLongPtrW(program->hwnds[1], GWL_EXSTYLE);
    LONG_PTR menu = GetWindowLongPtrW(program->hwnds[2], GWL_STYLE);
    LONG_PTR menuExtended = GetWindowLongPtrW(program->hwnds[2], GWL_EXSTYLE);
    LONG_PTR tooltip = GetWindowLongPtrW(program->hwnds[3], GWL_EXSTYLE);
    CHECK(GetWindow(program->hwnds[1], GW_OWNER) == program->hwnds[0] &&
              (dialog & WS_EX_APPWINDOW) == 0,
          "an owned window owned by its owner's, off the taskbar");
    CHECK(GetWindow(program->hwnds[2], GW_OWNER) == program->hwnds[0] &&
              (menu & (WS_POPUP | WS_CAPTION | WS_THICKFRAME)) == WS_POPUP &&
              (menuExtended & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) == WS_EX_TOOLWINDOW,
          "a menu an owned undecorated tool window");
    CHECK((tooltip & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) ==
                  (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) &&
              GetActiveWindow() != program->hwnds[3],
          "a tooltip never activated");
    CHECK(Placed(program, 10.0f, 20.0f) && program->menuMoves == 1 &&
              program->menuPlace.x == 10.0f && program->menuPlace.y == 20.0f,
          "a menu at its place against its owner's client area, reported so");
}

// Whether the step's work is seen yet.
static bool Settled(const Program* program)
{
    switch (program->step)
    {
    case 1:
        return program->created == 4 && program->menuMoves > 0;
    case 2:
        return Placed(program, 10.0f, 20.0f);
    case 3:
        return Placed(program, 5.0f, 6.0f) && program->menuPlace.x == 5.0f;
    default:
        return program->menuClose;
    }
}

static void Next(mwinContext* context, Program* program)
{
    mwinWindowState state;
    switch (program->step)
    {
    case 0:
        program->dialog = Create(context, program->owner, mwin_windowNormal, (mwinPosition){0});
        program->menu = Create(context, program->owner, mwin_windowMenu, (mwinPosition){10, 20});
        program->tooltip =
            Create(context, program->owner, mwin_windowTooltip, (mwinPosition){30, 40});
        break;
    case 1:
        for (int i = 0; i < 4; i++)
        {
            mwinWindowId ids[4] = {program->owner, program->dialog, program->menu,
                                   program->tooltip};
            program->hwnds[i] = Handle(context, ids[i]);
        }
        CheckMade(program);
        CHECK(mwinGetWindowState(context, program->owner, &state) == mwin_success &&
                  mwinRequestPosition(
                      context, program->owner,
                      (mwinPosition){state.position.x + 60.0f, state.position.y + 40.0f},
                      nullptr) == mwin_success,
              "move the owner");
        break;
    case 2:
        CHECK(program->menuMoves == 1, "a menu following its owner keeps its place");
        CHECK(mwinRequestPosition(context, program->menu, (mwinPosition){5.0f, 6.0f}, nullptr) ==
                  mwin_success,
              "place the menu");
        break;
    case 3:
        CHECK(mwinGetWindowState(context, program->menu, &state) == mwin_success &&
                  state.position.x == 5.0f && state.position.y == 6.0f,
              "a menu placed again against its owner");
        SetActiveWindow(program->hwnds[0]);
        break;
    default:
        CHECK(mwinDestroyWindow(context, program->owner) == mwin_success, "destroy the owner");
        CHECK(!IsWindow(program->hwnds[1]) && !IsWindow(program->hwnds[2]) &&
                  !IsWindow(program->hwnds[3]),
              "owned windows gone with their owner");
        program->done = true;
        break;
    }
    program->step += 1;
    program->stepMs = GetTickCount64();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){400.0f, 300.0f};
    return mwinCreateWindow(context, &def, &program->owner, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    static const char* const what[] = {"", "the windows made", "the menu followed its owner",
                                       "the menu placed again",
                                       "a menu asked to close when the keyboard goes elsewhere"};
    Program* program = user;
    Collect(context, program);
    if (program->step == 0 && program->created == 1)
    {
        Next(context, program);
    }
    else if (program->step > 0 && !program->done)
    {
        bool settled = Settled(program);
        if (settled || GetTickCount64() - program->stepMs > STEP_MS)
        {
            CHECK(settled, what[program->step]);
            Next(context, program);
        }
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
