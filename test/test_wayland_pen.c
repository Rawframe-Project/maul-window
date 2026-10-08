// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The pen on Wayland (mwin-0037), against the test compositor of
// wayland_server.h with the tablet of wayland_tablet_server.h: a pen
// coming near the window moves, hovering without pressure, tilted, with
// the window's cursor shape set for it; it presses down with half the
// pressure, clicks its barrel button, and lifts, the button's release
// before the lift; the eraser comes near with its flag and no tilt, and
// leaving while down lifts it. No mouse record comes from any of it.
// Skipped (exit status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_tablet_server.h"

#include "maul-window/event.h"
#include "maul-window/window.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 32

typedef enum Phase
{
    phaseCreate,
    phaseNear,
    phaseDown,
    phaseBarrel,
    phaseUp,
    phaseEraser,
    phaseLeft,
    phaseDone,
} Phase;

typedef struct Program
{
    TabletServer tablet;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinEvent records[MAX_RECORDS];
    int count;
    int mouseRecords;
    bool timedOut;
} Program;

static Server s_server;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

// The index of the first record of a type, or -1.
static int Find(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type)
        {
            return i;
        }
    }
    return -1;
}

static const mwinPenEvent* PenOf(const Program* program, mwinEventType type)
{
    int found = Find(program, type);
    return found >= 0 ? &program->records[found].data.pen : nullptr;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static TabletChange Change(void)
{
    return (TabletChange){.pressure = -1, .tiltX = (double)NAN, .tiltY = (double)NAN};
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventWindowCreated) >= 0;
    case phaseNear:
    case phaseEraser:
        return Find(program, mwin_eventPenMoved) >= 0;
    case phaseDown:
        return Find(program, mwin_eventPenDown) >= 0;
    case phaseBarrel:
        // The compositor sees the pen's cursor in its own time.
        return Find(program, mwin_eventPenButtonDown) >= 0 &&
               ServerCursor(&s_server).toolShape == WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    case phaseUp:
    case phaseLeft:
        return Find(program, mwin_eventPenUp) >= 0;
    default:
        return false;
    }
}

static void AdvanceLate(Program* program)
{
    TabletChange change = Change();
    const mwinPenEvent* pen = nullptr;
    switch (program->phase)
    {
    case phaseUp:
        CHECK(Find(program, mwin_eventPenButtonUp) >= 0 &&
                  Find(program, mwin_eventPenButtonUp) < Find(program, mwin_eventPenUp),
              "the button's release before the lift");
        pen = PenOf(program, mwin_eventPenUp);
        CHECK(pen->flags == 0 && pen->pressure == 0.0f, "lifted: no contact, no barrel");
        change.left = true;
        TabletSend(&program->tablet, tabletPen, change);
        change = Change();
        change.near = true;
        change.moves = true;
        change.x = 5.0;
        change.y = 5.0;
        change.pressure = 0;
        TabletSend(&program->tablet, tabletEraser, change);
        break;
    case phaseEraser:
        pen = PenOf(program, mwin_eventPenMoved);
        CHECK(pen->flags == mwin_penEraser && pen->tiltX == 0.0f && pen->tiltY == 0.0f &&
                  Near(pen->position.x, 5.0f),
              "the eraser near, with its flag and no tilt");
        change.down = true;
        change.pressure = 40000;
        TabletSend(&program->tablet, tabletEraser, change);
        change = Change();
        change.left = true;
        TabletSend(&program->tablet, tabletEraser, change);
        break;
    default:
        CHECK(PenOf(program, mwin_eventPenDown) != nullptr &&
                  PenOf(program, mwin_eventPenDown)->flags == (mwin_penEraser | mwin_penContact) &&
                  PenOf(program, mwin_eventPenUp)->flags == mwin_penEraser,
              "leaving while down lifts the eraser");
        break;
    }
}

static void Advance(Program* program)
{
    TabletChange change = Change();
    const mwinPenEvent* pen = nullptr;
    switch (program->phase)
    {
    case phaseCreate:
        change.near = true;
        change.moves = true;
        change.x = 10.0;
        change.y = 20.0;
        change.pressure = 0;
        change.tiltX = 30.0;
        change.tiltY = -15.0;
        TabletSend(&program->tablet, tabletPen, change);
        break;
    case phaseNear:
        pen = PenOf(program, mwin_eventPenMoved);
        CHECK(Near(pen->position.x, 10.0f) && Near(pen->position.y, 20.0f) &&
                  pen->pressure == 0.0f && Near(pen->tiltX, 30.0f) && Near(pen->tiltY, -15.0f) &&
                  pen->flags == 0,
              "the pen near: hovering, tilted, without pressure");
        change.down = true;
        change.moves = true;
        change.x = 12.0;
        change.y = 22.0;
        change.pressure = 32768;
        TabletSend(&program->tablet, tabletPen, change);
        break;
    case phaseDown:
        pen = PenOf(program, mwin_eventPenDown);
        CHECK(pen->flags == mwin_penContact && Near(pen->pressure, 0.5f) &&
                  Near(pen->position.x, 12.0f),
              "down with half the pressure");
        change.button = BTN_STYLUS;
        change.pressed = true;
        TabletSend(&program->tablet, tabletPen, change);
        break;
    case phaseBarrel:
        pen = PenOf(program, mwin_eventPenButtonDown);
        CHECK(pen->button == 1 && pen->flags == (mwin_penContact | mwin_penBarrel),
              "the barrel button, while in contact");
        change.button = BTN_STYLUS;
        change.pressed = false;
        change.up = true;
        TabletSend(&program->tablet, tabletPen, change);
        break;
    default:
        AdvanceLate(program);
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->mouseRecords += event.type == mwin_eventCursorEntered ||
                                         event.type == mwin_eventCursorMoved ||
                                         event.type == mwin_eventButtonDown
                                     ? 1
                                     : 0;
        if (program->count < MAX_RECORDS)
        {
            program->records[program->count++] = event;
        }
    }
    if (Ready(program))
    {
        Advance(program);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 500000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&s_server, "us", ""))
    {
        return 77;
    }
    static Program program;
    TabletAdd(&program.tablet, &s_server);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    CHECK(program.mouseRecords == 0, "no mouse record from the tablet");
    ServerStop(&s_server);
    return s_failures == 0 ? 0 : 1;
}
