// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's custom chrome, driven through XTEST and read back
// by another client that listens on the root window as a window manager
// would:
// - a decorated window of custom chrome asks for no decorations;
// - a left press on a caption or an edge becomes _NET_WM_MOVERESIZE
//   with its place on the desktop, its direction and its button, and
//   the program is told of neither it nor its release;
// - a press on a maximize button is the program's;
// - a double click on a caption asks _NET_WM_STATE for both maximized
//   states.
// Skipped (exit status 77) without an X server.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

#define DEADLINE_NS 10000000000ull
#define STEP_NS     3000000000ull
#define POINTER_NS  2000000000ull

enum
{
    atomMoveresize,
    atomState,
    atomMaximizedVert,
    atomMaximizedHorz,
    atomMotif,
    atoms,
};

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_window_t root;
    xcb_atom_t atoms[atoms];
    uint64_t startNs;
    uint64_t stepNs;
    int step;
    mwinWindowId window;
    xcb_window_t handle;
    xcb_point_t origin;
    bool created;
    bool regions;
    int downs;
    int ups;
    // The last _NET_WM_MOVERESIZE and _NET_WM_STATE messages, and how
    // many came.
    uint32_t moveresize[5];
    int moveresizes;
    uint32_t state[5];
    int states;
    bool done;
} Program;

static const mwinHitRegion s_regions[3] = {
    {{0.0f, 0.0f, 400.0f, 30.0f}, mwin_hitCaption},
    {{340.0f, 0.0f, 30.0f, 30.0f}, mwin_hitMaximize},
    {{0.0f, 30.0f, 4.0f, 270.0f}, mwin_hitLeft},
};

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Collect(mwinContext* context, Program* program)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        bool done = event.type == mwin_eventRequestCompleted &&
                    event.data.completion.outcome == mwin_outcomeDone;
        program->regions = program->regions || (done && program->created);
        program->created = program->created || done;
        program->downs += event.type == mwin_eventButtonDown;
        program->ups += event.type == mwin_eventButtonUp;
    }
    xcb_generic_event_t* got;
    while ((got = xcb_poll_for_event(program->connection)) != nullptr)
    {
        const xcb_client_message_event_t* message = (const xcb_client_message_event_t*)got;
        if ((got->response_type & 0x7F) == XCB_CLIENT_MESSAGE && message->window == program->handle)
        {
            bool moveresize = message->type == program->atoms[atomMoveresize];
            bool state = message->type == program->atoms[atomState];
            memcpy(moveresize ? program->moveresize : program->state, message->data.data32,
                   moveresize || state ? sizeof(program->state) : 0);
            program->moveresizes += moveresize;
            program->states += state;
        }
        free(got);
    }
}

// A left click at a point of the client area in logical units (the
// test's X server has a scale of 1).
static void Click(const Program* program, int16_t x, int16_t y)
{
    xcb_connection_t* connection = program->connection;
    int16_t rootX = (int16_t)(program->origin.x + x);
    int16_t rootY = (int16_t)(program->origin.y + y);
    xcb_test_fake_input(connection, XCB_MOTION_NOTIFY, 0, XCB_CURRENT_TIME, program->root, rootX,
                        rootY, 0);
    // A press the X server takes before the pointer got there would go
    // where it was: XTEST's motion waits in the server's input queue,
    // which a loaded machine drains late, so the wait is by time, not by
    // a number of round trips (a double click's first press once went to
    // the button clicked before).
    bool there = false;
    uint64_t startNs = NowNs();
    while (!there && NowNs() - startNs < POINTER_NS)
    {
        xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(
            connection, xcb_query_pointer(connection, program->root), nullptr);
        there = pointer != nullptr && pointer->root_x == rootX && pointer->root_y == rootY;
        free(pointer);
        if (!there)
        {
            struct timespec pause = {0, 1000000};
            (void)nanosleep(&pause, nullptr);
        }
    }
    CHECK(there, "the pointer at the click's place");
    xcb_test_fake_input(connection, XCB_BUTTON_PRESS, 1, XCB_CURRENT_TIME, program->root, 0, 0, 0);
    xcb_test_fake_input(connection, XCB_BUTTON_RELEASE, 1, XCB_CURRENT_TIME, program->root, 0, 0,
                        0);
    xcb_flush(connection);
}

static uint32_t Property(const Program* program, xcb_atom_t property, int index)
{
    xcb_get_property_reply_t* reply = xcb_get_property_reply(
        program->connection,
        xcb_get_property(program->connection, 0, program->handle, property, property, 0, 5),
        nullptr);
    uint32_t value = 0xFFFFFFFFu;
    if (reply != nullptr && reply->format == 32 && xcb_get_property_value_length(reply) == 20)
    {
        value = ((const uint32_t*)xcb_get_property_value(reply))[index];
    }
    free(reply);
    return value;
}

static void Locate(mwinContext* context, Program* program)
{
    mwinNativeHandles handles;
    CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success, "handles");
    program->handle = (xcb_window_t)handles.handles.x11.window;
    xcb_translate_coordinates_reply_t* reply = xcb_translate_coordinates_reply(
        program->connection,
        xcb_translate_coordinates(program->connection, program->handle, program->root, 0, 0),
        nullptr);
    program->origin =
        (xcb_point_t){reply != nullptr ? reply->dst_x : 0, reply != nullptr ? reply->dst_y : 0};
    free(reply);
}

static bool MovedFrom(const Program* program, int16_t x, int16_t y, uint32_t direction)
{
    const uint32_t* data = program->moveresize;
    return data[0] == (uint32_t)(program->origin.x + x) &&
           data[1] == (uint32_t)(program->origin.y + y) && data[2] == direction && data[3] == 1 &&
           data[4] == 1;
}

// Whether the step's work is seen yet.
static bool Settled(const Program* program)
{
    switch (program->step)
    {
    case 1:
        return program->origin.x == 70 && program->origin.y == 50;
    case 2:
        return program->regions;
    case 3:
        return program->moveresizes == 1;
    case 4:
        return program->moveresizes == 2;
    case 5:
        return program->downs == 1 && program->ups == 1;
    default:
        return program->states == 1;
    }
}

static void Next(mwinContext* context, Program* program)
{
    switch (program->step)
    {
    case 0:
        // Away from the root's corner, so a place on the desktop is not
        // one in the window.
        CHECK(mwinRequestPosition(context, program->window, (mwinPosition){70, 50}, nullptr) ==
                  mwin_success,
              "move");
        break;
    case 1:
        CHECK(Property(program, program->atoms[atomMotif], 2) == 0,
              "a decorated window of custom chrome asks for no decorations");
        CHECK(mwinRequestHitRegions(context, program->window, s_regions, 3, nullptr) ==
                  mwin_success,
              "regions");
        break;
    case 2:
        Click(program, 100, 10);
        break;
    case 3:
        CHECK(MovedFrom(program, 100, 10, 8) && program->downs == 0 && program->ups == 0,
              "a press on a caption a move by the window manager, not the program's");
        Click(program, 2, 100);
        break;
    case 4:
        CHECK(MovedFrom(program, 2, 100, 7) && program->downs == 0 && program->ups == 0,
              "a press on the left edge a resize from it by the window manager");
        Click(program, 350, 10);
        break;
    case 5:
        Click(program, 100, 10);
        Click(program, 100, 10);
        break;
    default:
        CHECK(program->moveresizes == 3 && program->state[0] == 1 &&
                  program->state[1] == program->atoms[atomMaximizedVert] &&
                  program->state[2] == program->atoms[atomMaximizedHorz],
              "a double click on a caption asks for both maximized states");
        program->done = true;
        break;
    }
    program->step += 1;
    program->stepNs = NowNs();
}

static void Relocate(mwinContext* context, Program* program)
{
    if (program->step == 1)
    {
        Locate(context, program);
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){400.0f, 300.0f};
    def.style = mwin_styleResizable | mwin_styleDecorated | mwin_styleCustomChrome;
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    static const char* const what[] = {"",
                                       "the window moved",
                                       "the regions taken",
                                       "the caption's move",
                                       "the edge's resize",
                                       "the maximize button's press and release",
                                       "the caption's double click"};
    Program* program = user;
    Collect(context, program);
    Relocate(context, program);
    if (program->step == 0 && program->created)
    {
        Next(context, program);
    }
    else if (program->step > 0 && !program->done)
    {
        bool settled = Settled(program);
        if (settled || NowNs() - program->stepNs > STEP_NS)
        {
            if (!settled)
            {
                // What the window manager and the program were told, for a
                // failure that does not repeat at will.
                printf("step %d: %d moveresizes (last %u at %u,%u), %d states, %d downs, "
                       "%d ups\n",
                       program->step, program->moveresizes, program->moveresize[2],
                       program->moveresize[0], program->moveresize[1], program->states,
                       program->downs, program->ups);
            }
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
    program.root = xcb_setup_roots_iterator(xcb_get_setup(program.connection)).data->root;
    const uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_change_window_attributes(program.connection, program.root, XCB_CW_EVENT_MASK, &mask);
    static const char* const names[atoms] = {"_NET_WM_MOVERESIZE", "_NET_WM_STATE",
                                             "_NET_WM_STATE_MAXIMIZED_VERT",
                                             "_NET_WM_STATE_MAXIMIZED_HORZ", "_MOTIF_WM_HINTS"};
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
