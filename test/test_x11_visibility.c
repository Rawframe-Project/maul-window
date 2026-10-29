// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's occlusion, as the X server computes it: another
// client's window covers the program's whole window (occluded), then
// only part of it (revealed: some of it can be seen), then all of it
// again (occluded); the program's window is hidden, still occluded, the
// cover goes while it is hidden, and shown again it is revealed, as the
// X server tells a window anew when it is mapped; the window's state
// says the same each time. The fixture's Xvfb runs no compositing manager,
// under which the X server would report no window covered. Skipped
// (exit status 77) without an X server.

#include "test_harness.h"
#include "x11_display.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <xcb/xcb.h>

#define DEADLINE_NS 5000000000ull
// How far the cover reaches past the window on every side, in pixels.
#define MARGIN 10

typedef enum Phase
{
    phaseCreate,
    phaseCovered,
    phasePartly,
    phaseCoveredAgain,
    phaseHidden,
    phaseUncovered,
    phaseDone,
} Phase;

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_window_t cover;
    // The window's place on the root window and its size, in pixels.
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    mwinWindowId window;
    mwinRequestId request;
    Phase phase;
    bool created;
    bool shown;
    bool hidden;
    // The occlusion notification the phase saw last, or 0.
    mwinEventType occlusion;
    uint64_t startNs;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        program->created =
            program->created || (event.type == mwin_eventRequestCompleted &&
                                 completion->request.index1 == program->request.index1 &&
                                 completion->request.generation == program->request.generation &&
                                 completion->outcome == mwin_outcomeDone);
        program->shown = program->shown || event.type == mwin_eventShown;
        program->hidden = program->hidden || event.type == mwin_eventHidden;
        if (event.type == mwin_eventOccluded || event.type == mwin_eventRevealed)
        {
            program->occlusion = event.type;
        }
    }
}

// Whether the window's state says it is occluded.
static bool Occluded(const Program* program, mwinContext* context)
{
    mwinWindowState state;
    return mwinWindow_GetState(context, program->window, &state) == mwin_success && state.occluded;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return program->created && program->shown;
    case phaseCovered:
    case phaseCoveredAgain:
        return program->occlusion == mwin_eventOccluded;
    case phaseHidden:
        return program->hidden;
    case phasePartly:
        return program->occlusion == mwin_eventRevealed;
    case phaseUncovered:
        return program->occlusion == mwin_eventRevealed && program->shown;
    default:
        return false;
    }
}

// Reads where the X server put the window and how large it made it.
static void Measure(Program* program, mwinContext* context)
{
    mwinNativeHandles handles;
    CHECK(mwinWindow_GetNativeHandles(context, program->window, &handles) == mwin_success,
          "the window's handle");
    xcb_window_t window = (xcb_window_t)handles.handles.x11.window;
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(program->connection)).data;
    xcb_translate_coordinates_reply_t* place = xcb_translate_coordinates_reply(
        program->connection,
        xcb_translate_coordinates(program->connection, window, screen->root, 0, 0), nullptr);
    xcb_get_geometry_reply_t* size = xcb_get_geometry_reply(
        program->connection, xcb_get_geometry(program->connection, window), nullptr);
    CHECK(place != nullptr && size != nullptr, "the window's place and size");
    if (place != nullptr && size != nullptr)
    {
        program->x = place->dst_x;
        program->y = place->dst_y;
        program->width = size->width;
        program->height = size->height;
    }
    free(place);
    free(size);
}

// Places the cover over all of the window, or over its left half only.
static void Cover(Program* program, bool whole)
{
    uint32_t values[4] = {(uint32_t)(program->x - MARGIN), (uint32_t)(program->y - MARGIN),
                          whole ? program->width + 2u * MARGIN : program->width / 2u + MARGIN,
                          program->height + 2u * MARGIN};
    xcb_configure_window(program->connection, program->cover,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                             XCB_CONFIG_WINDOW_HEIGHT,
                         values);
    xcb_flush(program->connection);
}

// Makes the other client's window, over the program's and above it.
static void MakeCover(Program* program)
{
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(program->connection)).data;
    program->cover = xcb_generate_id(program->connection);
    // Left alone by a window manager, were one running.
    uint32_t values[2] = {screen->black_pixel, 1};
    xcb_create_window(program->connection, XCB_COPY_FROM_PARENT, program->cover, screen->root,
                      (int16_t)(program->x - MARGIN), (int16_t)(program->y - MARGIN),
                      (uint16_t)(program->width + 2u * MARGIN),
                      (uint16_t)(program->height + 2u * MARGIN), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT, values);
    xcb_map_window(program->connection, program->cover);
    xcb_flush(program->connection);
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(program->occlusion != mwin_eventOccluded && !Occluded(program, context),
              "a window nothing covers is not occluded");
        Measure(program, context);
        MakeCover(program);
        break;
    case phaseCovered:
        CHECK(Occluded(program, context), "a window another covers wholly is occluded");
        Cover(program, false);
        break;
    case phasePartly:
        CHECK(!Occluded(program, context), "a window covered in part is revealed");
        Cover(program, true);
        break;
    case phaseCoveredAgain:
        CHECK(Occluded(program, context), "a window covered wholly again is occluded");
        CHECK(mwinWindow_RequestVisible(context, program->window, false, nullptr) == mwin_success,
              "hidden");
        break;
    case phaseHidden:
        CHECK(Occluded(program, context) && program->occlusion == 0,
              "a covered window hidden stays occluded");
        // The cover goes while the window is unmapped, which the X server
        // has carried out once its answer comes; then the window is shown.
        xcb_unmap_window(program->connection, program->cover);
        free(xcb_get_input_focus_reply(program->connection,
                                       xcb_get_input_focus(program->connection), nullptr));
        program->shown = false;
        CHECK(mwinWindow_RequestVisible(context, program->window, true, nullptr) == mwin_success,
              "shown");
        break;
    case phaseUncovered:
        CHECK(!Occluded(program, context), "a window shown again with nothing over it is revealed");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->occlusion = (mwinEventType)0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){200.0f, 100.0f};
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d, last occlusion notification %d\n", (int)program->phase,
                     (int)program->occlusion);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 1000000};
        // hygiene: yields between frames; the wait ends on a report, within the deadline
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    DisplayRequired();
    (void)unsetenv("WAYLAND_DISPLAY");
    static Program program;
    program.connection = DisplayConnect();
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    CHECK(!program.timedOut && program.phase == phaseDone, "every phase completes in time");
    xcb_disconnect(program.connection);
    return s_failures == 0 ? 0 : 1;
}
