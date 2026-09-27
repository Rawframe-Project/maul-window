// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's owned and popup windows, read back by another
// client: an owned window transient for its owner's and a dialog; a
// menu override-redirect, transient, a popup menu at its place against
// its owner, taking the keyboard, following its owner, placed again
// against it and asked to close when the keyboard goes elsewhere; a
// tooltip override-redirect and never focused; and all gone with their
// owner. Skipped (exit status 77) without an X server.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#define DEADLINE_NS 10000000000ull
#define STEP_NS     3000000000ull

enum
{
    atomType,
    atomDialog,
    atomMenu,
    atomTooltip,
    atoms,
};

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_atom_t atoms[atoms];
    uint64_t startNs;
    uint64_t stepNs;
    int step;
    mwinWindowId ids[4];
    xcb_window_t windows[4];
    int created;
    int menuMoves;
    mwinPosition menuPlace;
    bool menuClose;
    bool tooltipDenied;
    bool done;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

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
        program->tooltipDenied =
            program->tooltipDenied || (event.type == mwin_eventRequestCompleted &&
                                       event.data.completion.outcome == mwin_outcomeDenied &&
                                       Same(event.window, program->ids[3]));
        if (event.type == mwin_eventMoved && Same(event.window, program->ids[2]))
        {
            program->menuMoves += 1;
            program->menuPlace = event.data.position;
        }
        program->menuClose = program->menuClose || (event.type == mwin_eventCloseRequested &&
                                                    Same(event.window, program->ids[2]));
    }
}

static xcb_window_t Handle(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (xcb_window_t)handles.handles.x11.window
               : 0;
}

// A window's single 32-bit property of a type, or 0.
static uint32_t Property(const Program* program, xcb_window_t window, xcb_atom_t property,
                         xcb_atom_t type)
{
    xcb_get_property_reply_t* reply = xcb_get_property_reply(
        program->connection, xcb_get_property(program->connection, 0, window, property, type, 0, 1),
        nullptr);
    uint32_t value = 0;
    if (reply != nullptr && reply->format == 32 && xcb_get_property_value_length(reply) == 4)
    {
        memcpy(&value, xcb_get_property_value(reply), 4);
    }
    free(reply);
    return value;
}

static bool Overriding(const Program* program, xcb_window_t window)
{
    xcb_get_window_attributes_reply_t* reply = xcb_get_window_attributes_reply(
        program->connection, xcb_get_window_attributes(program->connection, window), nullptr);
    bool overriding = reply != nullptr && reply->override_redirect != 0;
    free(reply);
    return overriding;
}

static bool Exists(const Program* program, xcb_window_t window)
{
    xcb_get_window_attributes_reply_t* reply = xcb_get_window_attributes_reply(
        program->connection, xcb_get_window_attributes(program->connection, window), nullptr);
    free(reply);
    return reply != nullptr;
}

static xcb_window_t Focused(const Program* program)
{
    xcb_get_input_focus_reply_t* reply = xcb_get_input_focus_reply(
        program->connection, xcb_get_input_focus(program->connection), nullptr);
    xcb_window_t focus = reply != nullptr ? reply->focus : 0;
    free(reply);
    return focus;
}

static xcb_point_t Origin(const Program* program, xcb_window_t window)
{
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(program->connection)).data;
    xcb_translate_coordinates_reply_t* reply = xcb_translate_coordinates_reply(
        program->connection,
        xcb_translate_coordinates(program->connection, window, screen->root, 0, 0), nullptr);
    xcb_point_t origin = {reply != nullptr ? reply->dst_x : -1,
                          reply != nullptr ? reply->dst_y : -1};
    free(reply);
    return origin;
}

// Whether the menu is at an offset in pixels (the X server of the test
// has a scale of 1) from its owner.
static bool Placed(const Program* program, int16_t x, int16_t y)
{
    xcb_point_t owner = Origin(program, program->windows[0]);
    xcb_point_t menu = Origin(program, program->windows[2]);
    return menu.x - owner.x == x && menu.y - owner.y == y;
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
    const xcb_window_t* windows = program->windows;
    const xcb_atom_t* types = program->atoms;
    CHECK(Property(program, windows[1], XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW) == windows[0] &&
              Property(program, windows[1], types[atomType], XCB_ATOM_ATOM) == types[atomDialog] &&
              !Overriding(program, windows[1]),
          "an owned window transient for its owner's and a dialog");
    CHECK(Property(program, windows[2], XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW) == windows[0] &&
              Property(program, windows[2], types[atomType], XCB_ATOM_ATOM) == types[atomMenu] &&
              Overriding(program, windows[2]),
          "a menu override-redirect, transient and a popup menu");
    CHECK(Property(program, windows[3], types[atomType], XCB_ATOM_ATOM) == types[atomTooltip] &&
              Overriding(program, windows[3]),
          "a tooltip override-redirect");
    CHECK(program->menuMoves == 1 && program->menuPlace.x == 10.0f && program->menuPlace.y == 20.0f,
          "a menu reported at its place against its owner");
}

// Whether the step's work is seen yet.
static bool Settled(const Program* program)
{
    switch (program->step)
    {
    case 1:
        return program->created == 4 && Placed(program, 10, 20) &&
               Focused(program) == program->windows[2];
    case 2:
        return Placed(program, 10, 20) && Origin(program, program->windows[0]).x == 60;
    case 3:
        return program->menuClose;
    case 4:
        return Placed(program, 5, 6) && program->menuPlace.x == 5.0f;
    default:
        return !Exists(program, program->windows[1]) && !Exists(program, program->windows[2]) &&
               !Exists(program, program->windows[3]);
    }
}

static void Next(mwinContext* context, Program* program)
{
    mwinWindowState state;
    mwinWindowId owner = program->ids[0];
    switch (program->step)
    {
    case 0:
        program->ids[1] = Create(context, owner, mwin_windowNormal, (mwinPosition){0});
        program->ids[2] = Create(context, owner, mwin_windowMenu, (mwinPosition){10, 20});
        program->ids[3] = Create(context, owner, mwin_windowTooltip, (mwinPosition){30, 40});
        break;
    case 1:
        CheckMade(program);
        CHECK(mwinRequestFocus(context, program->ids[3], nullptr) == mwin_success,
              "ask to focus the tooltip");
        CHECK(mwinRequestPosition(context, owner, (mwinPosition){60, 40}, nullptr) == mwin_success,
              "move the owner");
        break;
    case 2:
        CHECK(program->tooltipDenied && Focused(program) == program->windows[2],
              "a tooltip never focused");
        xcb_set_input_focus(program->connection, XCB_INPUT_FOCUS_POINTER_ROOT, program->windows[0],
                            XCB_CURRENT_TIME);
        xcb_flush(program->connection);
        break;
    case 3:
        // The X server's events come in order: the menu's move with its
        // owner was read before the keyboard left it.
        CHECK(program->menuMoves == 1, "a menu following its owner keeps its place");
        CHECK(mwinRequestPosition(context, program->ids[2], (mwinPosition){5, 6}, nullptr) ==
                  mwin_success,
              "place the menu");
        break;
    case 4:
        CHECK(mwinGetWindowState(context, program->ids[2], &state) == mwin_success &&
                  state.position.x == 5.0f && state.position.y == 6.0f,
              "a menu placed again against its owner");
        CHECK(mwinDestroyWindow(context, owner) == mwin_success, "destroy the owner");
        break;
    default:
        program->done = true;
        break;
    }
    program->step += 1;
    program->stepNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){400.0f, 300.0f};
    return mwinCreateWindow(context, &def, &program->ids[0], nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    static const char* const what[] = {"",
                                       "the windows made, the menu focused",
                                       "the menu followed its owner",
                                       "a menu asked to close when the keyboard goes elsewhere",
                                       "the menu placed again",
                                       "owned windows gone with their owner"};
    Program* program = user;
    Collect(context, program);
    if (program->step == 0 && program->created == 1)
    {
        Next(context, program);
        for (int i = 0; i < 4; i++)
        {
            program->windows[i] = Handle(context, program->ids[i]);
        }
    }
    else if (program->step > 0 && !program->done)
    {
        bool settled = Settled(program);
        if (settled || NowNs() - program->stepNs > STEP_NS)
        {
            CHECK(settled, what[program->step]);
            Next(context, program);
        }
    }
    struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, nullptr);
    bool late = NowNs() - program->startNs > DEADLINE_NS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    if (getenv("DISPLAY") == nullptr)
    {
        return 77;
    }
    (void)unsetenv("WAYLAND_DISPLAY");
    static Program program;
    program.connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(program.connection) != 0)
    {
        return 77;
    }
    static const char* const names[atoms] = {"_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DIALOG",
                                             "_NET_WM_WINDOW_TYPE_POPUP_MENU",
                                             "_NET_WM_WINDOW_TYPE_TOOLTIP"};
    for (int i = 0; i < atoms; i++)
    {
        xcb_intern_atom_reply_t* atom = xcb_intern_atom_reply(
            program.connection,
            xcb_intern_atom(program.connection, 0, (uint16_t)strlen(names[i]), names[i]), nullptr);
        program.atoms[i] = atom != nullptr ? atom->atom : XCB_ATOM_NONE;
        free(atom);
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs");
    xcb_disconnect(program.connection);
    return s_failures == 0 ? 0 : 1;
}
