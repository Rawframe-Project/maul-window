// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's window manager hints and states, read back and set
// by another client that listens on the root window as a window manager
// would:
// - a window that does not resize bounded to its size, and one that
//   does unbounded, with no aspect ratio;
// - a window always on top made with _NET_WM_STATE_ABOVE, others
//   without;
// - size limits with a minimum under a pixel kept at one pixel, and a
//   maximum of one side leaving the other free;
// - an opacity set, and lifted at 1;
// - a client message other than WM_PROTOCOLS no close request;
// - both maximized states set by the window manager a maximized mode;
// - a maximized mode asked of the window manager by adding both states,
//   the test's check window naming itself as a running window manager's
//   does, and unsupported while it does not, as one left behind by a
//   window manager that quit.
// Skipped (exit status 77) without an X server.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#define DEADLINE_NS 10000000000ull

// The fields of WM_NORMAL_HINTS read here.
enum
{
    hintFlags = 0,
    hintMinimumWidth = 5,
    hintMinimumHeight = 6,
    hintMaximumWidth = 7,
    hintMaximumHeight = 8,
    hintFields = 18,
};

#define HINT_MINIMUM 16u
#define HINT_MAXIMUM 32u
#define HINT_ASPECT  128u

enum
{
    atomState,
    atomAbove,
    atomVertical,
    atomHorizontal,
    atomOpacity,
    atomDelete,
    atomCheck,
    atomCount,
};

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_window_t root;
    xcb_atom_t atoms[atomCount];
    uint64_t startNs;
    int step;
    // The window that resizes and is always on top, and the one fixed.
    mwinWindowId ids[2];
    xcb_window_t windows[2];
    int created;
    // The requests done since the step began.
    int completed;
    bool maximized;
    bool closeAsked;
    // The _NET_WM_STATE message asking for the fixed window's maximized
    // states, its action.
    int maximizeAction;
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
        program->created += event.type == mwin_eventWindowCreated;
        program->completed += event.type == mwin_eventRequestCompleted &&
                              event.data.completion.outcome == mwin_outcomeDone;
        program->maximized = program->maximized || (event.type == mwin_eventModeChanged &&
                                                    event.data.mode == mwin_modeMaximized &&
                                                    Same(event.window, program->ids[0]));
        program->closeAsked = program->closeAsked || event.type == mwin_eventCloseRequested;
    }
    xcb_generic_event_t* got = nullptr;
    while ((got = xcb_poll_for_event(program->connection)) != nullptr)
    {
        const xcb_client_message_event_t* message = (const xcb_client_message_event_t*)got;
        if ((got->response_type & 0x7F) == XCB_CLIENT_MESSAGE &&
            message->window == program->windows[1] && message->type == program->atoms[atomState] &&
            message->data.data32[1] == program->atoms[atomVertical])
        {
            program->maximizeAction = (int)message->data.data32[0];
        }
        free(got);
    }
}

static xcb_window_t Handle(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (xcb_window_t)handles.handles.x11.window
               : 0;
}

// A window's 32-bit property values of a type: how many, up to count.
static int Values(const Program* program, xcb_window_t window, xcb_atom_t property, xcb_atom_t type,
                  uint32_t* values, int count)
{
    xcb_get_property_reply_t* reply = xcb_get_property_reply(
        program->connection,
        xcb_get_property(program->connection, 0, window, property, type, 0, (uint32_t)count),
        nullptr);
    int found =
        reply != nullptr && reply->format == 32 ? xcb_get_property_value_length(reply) / 4 : 0;
    if (found > 0)
    {
        memcpy(values, xcb_get_property_value(reply), (size_t)found * 4);
    }
    free(reply);
    return found;
}

static bool Hints(const Program* program, xcb_window_t window, uint32_t hints[hintFields])
{
    return Values(program, window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, hints,
                  hintFields) == hintFields;
}

static bool Above(const Program* program, xcb_window_t window)
{
    uint32_t states[8];
    int count = Values(program, window, program->atoms[atomState], XCB_ATOM_ATOM, states, 8);
    return count == 1 && states[0] == program->atoms[atomAbove];
}

static void Send(const Program* program, xcb_window_t window, xcb_atom_t type, uint32_t value)
{
    union
    {
        xcb_client_message_event_t message;
        char bytes[32];
    } event = {0};
    event.message.response_type = XCB_CLIENT_MESSAGE;
    event.message.format = 32;
    event.message.window = window;
    event.message.type = type;
    event.message.data.data32[0] = value;
    xcb_send_event(program->connection, 0, window, XCB_EVENT_MASK_NO_EVENT, event.bytes);
}

static void CheckMade(const Program* program)
{
    uint32_t hints[hintFields] = {0};
    CHECK(Hints(program, program->windows[1], hints) &&
              hints[hintFlags] == (HINT_MINIMUM | HINT_MAXIMUM) && hints[hintMinimumWidth] == 300 &&
              hints[hintMinimumHeight] == 150 && hints[hintMaximumWidth] == 300 &&
              hints[hintMaximumHeight] == 150,
          "a window that does not resize bounded to its size");
    CHECK(Hints(program, program->windows[0], hints) && hints[hintFlags] == 0,
          "one that does unbounded, with no aspect ratio");
    uint32_t states[8];
    CHECK(Above(program, program->windows[0]) &&
              Values(program, program->windows[1], program->atoms[atomState], XCB_ATOM_ATOM, states,
                     8) == 0,
          "a window always on top made with _NET_WM_STATE_ABOVE, others without");
}

static void CheckLimits(const Program* program)
{
    uint32_t hints[hintFields] = {0};
    CHECK(Hints(program, program->windows[0], hints) &&
              hints[hintFlags] == (HINT_MINIMUM | HINT_MAXIMUM) && hints[hintMinimumWidth] == 1 &&
              hints[hintMinimumHeight] == 0 && hints[hintMaximumWidth] == INT16_MAX &&
              hints[hintMaximumHeight] == 400,
          "a minimum under a pixel kept at one, and a maximum of one side");
    uint32_t opacity = 0;
    CHECK(Values(program, program->windows[0], program->atoms[atomOpacity], XCB_ATOM_CARDINAL,
                 &opacity, 1) == 1 &&
              opacity == 0x7FFFFFFFu,
          "an opacity set");
}

static bool Opaque(const Program* program)
{
    uint32_t opacity = 0;
    return Values(program, program->windows[0], program->atoms[atomOpacity], XCB_ATOM_CARDINAL,
                  &opacity, 1) == 0;
}

// Whether the step's work is seen yet: the requests done, and the X
// server, which takes the program's requests and the test's in either
// order, holding their changes.
static bool Settled(const Program* program)
{
    uint32_t hints[hintFields] = {0};
    switch (program->step)
    {
    case 0:
        return program->created == 2;
    case 1:
        return program->completed == 2 && Hints(program, program->windows[0], hints) &&
               hints[hintFlags] != 0 && !Opaque(program);
    case 2:
        return program->completed == 1 && Opaque(program);
    default:
        return program->maximized && program->maximizeAction >= 0;
    }
}

static void Advance(mwinContext* context, Program* program)
{
    const xcb_atom_t* atoms = program->atoms;
    switch (program->step)
    {
    case 0:
        program->windows[0] = Handle(context, program->ids[0]);
        program->windows[1] = Handle(context, program->ids[1]);
        CheckMade(program);
        CHECK(mwinRequestSizeLimits(context, program->ids[0], (mwinSize){0.2f, 0.0f},
                                    (mwinSize){0.0f, 400.0f}, nullptr) == mwin_success &&
                  mwinRequestOpacity(context, program->ids[0], 0.5f, nullptr) == mwin_success,
              "limits and an opacity");
        break;
    case 1:
        CheckLimits(program);
        CHECK(mwinRequestOpacity(context, program->ids[0], 1.0f, nullptr) == mwin_success,
              "an opacity of 1");
        break;
    case 2:
    {
        // Before the state, so that the program has read it once the
        // mode arrives.
        Send(program, program->windows[0], atoms[atomState], atoms[atomDelete]);
        const xcb_atom_t states[2] = {atoms[atomVertical], atoms[atomHorizontal]};
        xcb_change_property(program->connection, XCB_PROP_MODE_REPLACE, program->windows[0],
                            atoms[atomState], XCB_ATOM_ATOM, 32, 2, states);
        xcb_flush(program->connection);
        CHECK(mwinRequestMode(context, program->ids[1], mwin_modeMaximized, nullptr) ==
                  mwin_success,
              "a maximized mode asked");
        break;
    }
    default:
        CHECK(!program->closeAsked, "another client message no close request");
        CHECK(program->maximizeAction == 1, "a maximized mode asked by adding both states");
        program->done = true;
        break;
    }
    program->step += 1;
    program->completed = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){200.0f, 100.0f};
    def.style |= mwin_styleAlwaysOnTop;
    mwinResult result = mwinCreateWindow(context, &def, &program->ids[0], nullptr);
    def.size = (mwinSize){300.0f, 150.0f};
    def.style = mwin_styleDecorated;
    return result == mwin_success ? mwinCreateWindow(context, &def, &program->ids[1], nullptr)
                                  : result;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(context, program);
    if (Settled(program))
    {
        Advance(context, program);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        CHECK(false, "the step settles in time");
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 1000000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->done ? mwin_frameStop : mwin_frameContinue;
}

// A program that asks for a maximized mode once its window is made.
typedef struct Asking
{
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
    int outcome;
} Asking;

static mwinResult AskingInit(mwinContext* context, void* user)
{
    Asking* asking = user;
    asking->startNs = NowNs();
    mwinWindowDef def = mwinDefaultWindowDef();
    return mwinCreateWindow(context, &def, &asking->window, nullptr);
}

static mwinFrameResult AskingFrame(mwinContext* context, void* user)
{
    Asking* asking = user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventWindowCreated)
        {
            CHECK(mwinRequestMode(context, asking->window, mwin_modeMaximized, &asking->request) ==
                      mwin_success,
                  "a mode asked");
        }
        if (event.type == mwin_eventRequestCompleted &&
            event.data.completion.request.index1 == asking->request.index1 &&
            event.data.completion.request.generation == asking->request.generation)
        {
            asking->outcome = event.data.completion.outcome;
        }
    }
    struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, nullptr);
    return asking->outcome >= 0 || NowNs() - asking->startNs > DEADLINE_NS ? mwin_frameStop
                                                                           : mwin_frameContinue;
}

static xcb_atom_t Intern(xcb_connection_t* connection, const char* name)
{
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name), nullptr);
    xcb_atom_t atom = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        return 77;
    }
    Program program = {.maximizeAction = -1};
    program.connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(program.connection) != 0)
    {
        xcb_disconnect(program.connection);
        return 77;
    }
    static const char* const names[atomCount] = {"_NET_WM_STATE",
                                                 "_NET_WM_STATE_ABOVE",
                                                 "_NET_WM_STATE_MAXIMIZED_VERT",
                                                 "_NET_WM_STATE_MAXIMIZED_HORZ",
                                                 "_NET_WM_WINDOW_OPACITY",
                                                 "WM_DELETE_WINDOW",
                                                 "_NET_SUPPORTING_WM_CHECK"};
    for (int i = 0; i < atomCount; i++)
    {
        program.atoms[i] = Intern(program.connection, names[i]);
    }
    // A window manager's check window, gone with the test's connection,
    // and the messages a window manager would take.
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(program.connection)).data;
    program.root = screen->root;
    xcb_window_t check = xcb_generate_id(program.connection);
    xcb_create_window(program.connection, XCB_COPY_FROM_PARENT, check, program.root, -1, -1, 1, 1,
                      0, XCB_WINDOW_CLASS_INPUT_ONLY, screen->root_visual, 0, nullptr);
    xcb_change_property(program.connection, XCB_PROP_MODE_REPLACE, program.root,
                        program.atoms[atomCheck], XCB_ATOM_WINDOW, 32, 1, &check);
    xcb_flush(program.connection);
    unsetenv("WAYLAND_DISPLAY");
    Asking asking = {.outcome = -1};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = AskingInit;
    def.frame = AskingFrame;
    def.user = &asking;
    CHECK(mwinRun(&def) == mwin_success && asking.outcome == mwin_outcomeUnsupported,
          "a check window that does not name itself no window manager");
    xcb_change_property(program.connection, XCB_PROP_MODE_REPLACE, check, program.atoms[atomCheck],
                        XCB_ATOM_WINDOW, 32, 1, &check);
    const uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_change_window_attributes(program.connection, program.root, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(program.connection);
    def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs");
    xcb_delete_property(program.connection, program.root, program.atoms[atomCheck]);
    xcb_disconnect(program.connection);
    return s_failures == 0 ? 0 : 1;
}
