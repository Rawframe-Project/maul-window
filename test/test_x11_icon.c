// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's window icons, read back by another client: every
// image in _NET_WM_ICON as its width, height and ARGB pixels with
// straight alpha, in order; none removes the property. Skipped (exit
// status 77) without an X server.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#define DEADLINE_NS 5000000000ull

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_atom_t icon;
    mwinWindowId window;
    mwinRequestId request;
    int step;
    bool answered;
    uint64_t startNs;
    uint64_t stepNs;
    bool done;
} Program;

// 2 by 1 and 1 by 1, RGBA.
static const uint8_t s_wide[] = {255, 0, 0, 128, 0, 255, 0, 255};
static const uint8_t s_dot[] = {1, 2, 3, 4};

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

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

// The window's _NET_WM_ICON as cardinals: how many, into values.
static int ReadIcon(const Program* program, mwinContext* context, uint32_t* values, int capacity)
{
    mwinNativeHandles handles;
    if (mwinGetNativeHandles(context, program->window, &handles) != mwin_success)
    {
        return -1;
    }
    xcb_get_property_reply_t* reply = xcb_get_property_reply(
        program->connection,
        xcb_get_property(program->connection, 0, (xcb_window_t)handles.handles.x11.window,
                         program->icon, XCB_ATOM_CARDINAL, 0, (uint32_t)capacity),
        nullptr);
    int count =
        reply != nullptr && reply->format == 32 ? xcb_get_property_value_length(reply) / 4 : 0;
    if (count > 0)
    {
        memcpy(values, xcb_get_property_value(reply), (size_t)count * 4);
    }
    free(reply);
    return count;
}

// Whether the property holds what the step asked for yet: the
// backend's requests reach the server at its next flush, and another
// client's may be answered first.
static bool Settled(mwinContext* context, const Program* program)
{
    static const uint32_t expected[] = {2, 1, 0x80FF0000u, 0xFF00FF00u, 1, 1, 0x04010203u};
    uint32_t values[16];
    int count = ReadIcon(program, context, values, 16);
    return program->step == 1 ? count == 7 && memcmp(values, expected, sizeof(expected)) == 0
                              : count == 0;
}

static void Next(mwinContext* context, Program* program)
{
    if (program->step == 0)
    {
        mwinIconImage images[2] = {{2, 1, 8, s_wide}, {1, 1, 4, s_dot}};
        CHECK(mwinRequestIcon(context, program->window, images, 2, &program->request) ==
                  mwin_success,
              "two images");
    }
    else if (program->step == 1)
    {
        CHECK(mwinRequestIcon(context, program->window, nullptr, 0, &program->request) ==
                  mwin_success,
              "none");
    }
    program->step += 1;
    program->answered = false;
    program->stepNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){200.0f, 100.0f};
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    static const char* const what[] = {
        "",
        "every image as its size and ARGB pixels with straight alpha, in order",
        "none removes the property",
    };
    Program* program = user;
    program->answered = program->answered || Outcome(context, program->request) == mwin_outcomeDone;
    if (program->answered && program->step == 0)
    {
        Next(context, program);
    }
    else if (program->answered && program->step < 3)
    {
        bool settled = Settled(context, program);
        if (settled || NowNs() - program->stepNs > 2000000000u)
        {
            CHECK(settled, what[program->step]);
            Next(context, program);
        }
    }
    program->done = program->step == 3;
    struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, nullptr);
    return program->done || NowNs() - program->startNs > DEADLINE_NS ? mwin_frameStop
                                                                     : mwin_frameContinue;
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
    const char name[] = "_NET_WM_ICON";
    xcb_intern_atom_reply_t* atom = xcb_intern_atom_reply(
        program.connection, xcb_intern_atom(program.connection, 0, sizeof(name) - 1, name),
        nullptr);
    program.icon = atom != nullptr ? atom->atom : XCB_ATOM_NONE;
    free(atom);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs");
    xcb_disconnect(program.connection);
    return s_failures == 0 ? 0 : 1;
}
