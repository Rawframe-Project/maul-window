// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Monitor hotplug on the X11 backend, a RandR monitor set and deleted on
// the X server by the test's own connection as a desktop's settings
// would: one set while the program runs arrives as a monitor with its
// name, place and physical size; deleted and set again with another
// size at once, it changes; deleted, it is removed and its id goes stale, while the
// server's own monitor stays. Skipped (exit status 77) without DISPLAY
// or RandR 1.5.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <time.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 64
#define NAME        "MAUL-TEST-1"

typedef enum Phase
{
    phaseCreate,
    phaseAdded,
    phaseChanged,
    phaseRemoved,
    phaseDone,
} Phase;

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_window_t root;
    xcb_atom_t name;
    size_t before;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinMonitorId monitor;
    mwinEvent records[MAX_RECORDS];
    int count;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

// Sets the test's monitor, not tied to any output, as xrandr
// --setmonitor does. The request is built here: libxcb-randr 1.15's
// xcb_randr_set_monitor closes the connection with a length error.
static void SetMonitor(Program* program, int16_t x, uint16_t width, uint16_t height)
{
    static const xcb_protocol_request_t request = {2, &xcb_randr_id, XCB_RANDR_SET_MONITOR, 1};
    struct
    {
        uint8_t major;
        uint8_t minor;
        uint16_t length;
        xcb_window_t window;
    } head = {0, 0, 0, program->root};
    xcb_randr_monitor_info_t info = {
        .name = program->name,
        .x = x,
        .y = 0,
        .width = width,
        .height = height,
        .width_in_millimeters = 100,
        .height_in_millimeters = 60,
    };
    struct iovec parts[4] = {
        [2] = {&head, sizeof(head)},
        [3] = {&info, sizeof(info)},
    };
    (void)xcb_send_request(program->connection, 0, parts + 2, &request);
    xcb_flush(program->connection);
}

static const mwinEvent* Find(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

// The monitor of a name among the connected ones, or false.
static bool Named(const mwinContext* context, mwinMonitorId* found, mwinMonitorInfo* info)
{
    mwinMonitorId monitors[8];
    size_t count = 0;
    if (mwinGetMonitors(context, monitors, 8, &count) != mwin_success)
    {
        return false;
    }
    for (size_t i = 0; i < count; i++)
    {
        if (mwinGetMonitorInfo(context, monitors[i], info) == mwin_success &&
            info->nameLength == strlen(NAME) && memcmp(info->name, NAME, strlen(NAME)) == 0)
        {
            *found = monitors[i];
            return true;
        }
    }
    return false;
}

static size_t Monitors(const mwinContext* context)
{
    mwinMonitorId monitors[8];
    size_t count = 99;
    return mwinGetMonitors(context, monitors, 8, &count) == mwin_success ? count : 99;
}

static bool Same(mwinMonitorId a, mwinMonitorId b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

// Whether a record of a type came for the test's monitor.
static bool Came(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type &&
            (type == mwin_eventMonitorAdded ||
             Same(program->records[i].data.monitor, program->monitor)))
        {
            return true;
        }
    }
    return false;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventWindowCreated) != nullptr;
    case phaseAdded:
        return Came(program, mwin_eventMonitorAdded);
    case phaseChanged:
        return Came(program, mwin_eventMonitorChanged);
    case phaseRemoved:
        return Came(program, mwin_eventMonitorRemoved);
    default:
        return false;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    mwinMonitorInfo info;
    switch (program->phase)
    {
    case phaseCreate:
        program->before = Monitors(context);
        CHECK(program->before >= 1 && !Named(context, &program->monitor, &info),
              "the server's own monitor, and not the test's");
        SetMonitor(program, 640, 320, 200);
        break;
    case phaseAdded:
        CHECK(Named(context, &program->monitor, &info) &&
                  Monitors(context) == program->before + 1 && info.bounds.x == 640 &&
                  info.bounds.y == 0 && info.bounds.width == 320 && info.bounds.height == 200 &&
                  info.widthMm == 100 && info.heightMm == 60,
              "a monitor set arrives with its name, place and size");
        // The X server refuses a name it has: a desktop redefines a monitor
        // by deleting and setting it, here under a grab so that the
        // backend reads only the result.
        xcb_grab_server(program->connection);
        xcb_randr_delete_monitor(program->connection, program->root, program->name);
        SetMonitor(program, 640, 400, 300);
        xcb_ungrab_server(program->connection);
        xcb_flush(program->connection);
        break;
    case phaseChanged:
        CHECK(mwinGetMonitorInfo(context, program->monitor, &info) == mwin_success &&
                  info.bounds.width == 400 && info.bounds.height == 300,
              "set again, the same monitor changes");
        xcb_randr_delete_monitor(program->connection, program->root, program->name);
        xcb_flush(program->connection);
        break;
    default:
        CHECK(Monitors(context) == program->before &&
                  mwinGetMonitorInfo(context, program->monitor, &info) == mwin_errorStale,
              "deleted, it is removed and its id goes stale; the server's stays");
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
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
    if (Ready(program))
    {
        Advance(program, context);
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

// RandR 1.5, which has monitors.
static bool HasMonitors(xcb_connection_t* connection)
{
    xcb_randr_query_version_reply_t* version = xcb_randr_query_version_reply(
        connection, xcb_randr_query_version(connection, 1, 5), nullptr);
    bool has = version != nullptr && (version->major_version > 1 ||
                                      (version->major_version == 1 && version->minor_version >= 5));
    free(version);
    return has;
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        return 77;
    }
    unsetenv("WAYLAND_DISPLAY");
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection) != 0 || !HasMonitors(connection))
    {
        xcb_disconnect(connection);
        return 77;
    }
    xcb_intern_atom_reply_t* atom = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 0, strlen(NAME), NAME), nullptr);
    static Program program;
    program = (Program){.connection = connection,
                        .root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root,
                        .name = atom != nullptr ? atom->atom : XCB_ATOM_NONE};
    free(atom);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the X server");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    // A monitor a failed run left behind would trouble the next.
    xcb_randr_delete_monitor(connection, program.root, program.name);
    xcb_disconnect(connection);
    return s_failures == 0 ? 0 : 1;
}
