// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's input against a real X server (Xvfb in CI), driven
// through XTEST from a connection of the test's own: keys with their
// codes, meanings and text, Shift, the X server's repeats, the pointer
// entering and moving, a double click, the wheel, cursor shapes, a
// hidden cursor, a confined one whose grab another client then meets,
// and a captured one with XInput 2's raw motion. Without DISPLAY the
// test is skipped (exit status 77).

#include "test_harness.h"

#include "maul-window/event.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 256

// X11 key codes on a US layout: evdev codes plus 8.
#define KEYCODE_A       38
#define KEYCODE_SHIFT_L 50

typedef enum Phase
{
    phaseCreate,
    phaseFocus,
    phaseKeys,
    phaseShift,
    phaseRepeat,
    phasePointer,
    phaseClicks,
    phaseWheel,
    phaseShape,
    phaseConfine,
    phaseRelease,
    phaseCapture,
    phaseRaw,
    phaseUncapture,
    phaseDone,
} Phase;

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_window_t root;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[64];
    uint32_t textLength;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Fake(Program* program, uint8_t type, uint8_t detail, int16_t x, int16_t y)
{
    xcb_test_fake_input(program->connection, type, detail, XCB_CURRENT_TIME, program->root, x, y,
                        0);
    xcb_flush(program->connection);
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        if (event.type == mwin_eventTextInput &&
            program->textLength + event.data.text.length <= sizeof(program->text))
        {
            memcpy(program->text + program->textLength, event.data.text.text,
                   event.data.text.length);
            program->textLength += event.data.text.length;
        }
        program->records[program->count++] = event;
    }
}

static const mwinEvent* Find(const Program* program, mwinEventType type, int nth)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type && nth-- == 0)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

static int CountRepeats(const Program* program)
{
    int repeats = 0;
    for (int i = 0; i < program->count; i++)
    {
        repeats +=
            program->records[i].type == mwin_eventKeyDown && program->records[i].data.key.repeat;
    }
    return repeats;
}

// Whether another client can grab the pointer now.
static bool CanGrab(Program* program)
{
    xcb_grab_pointer_reply_t* reply = xcb_grab_pointer_reply(
        program->connection,
        xcb_grab_pointer(program->connection, 0, program->root, 0, XCB_GRAB_MODE_ASYNC,
                         XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME),
        nullptr);
    bool grabbed = reply != nullptr && reply->status == XCB_GRAB_STATUS_SUCCESS;
    free(reply);
    if (grabbed)
    {
        // A round trip: the X server has let the grab go before the
        // program's next grab can meet it.
        xcb_ungrab_pointer(program->connection, XCB_CURRENT_TIME);
        free(xcb_get_input_focus_reply(program->connection,
                                       xcb_get_input_focus(program->connection), nullptr));
    }
    return grabbed;
}

// Whether another client can grab the pointer within half a second: the
// X server serves its clients in its own order, so it may meet the
// test's grab before the program's ungrab, sent first.
static bool CanGrabSoon(Program* program)
{
    for (int i = 0; i < 100; i++)
    {
        if (CanGrab(program))
        {
            return true;
        }
        struct timespec pause = {0, 5000000};
        (void)nanosleep(&pause, nullptr);
    }
    return false;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventShown, 0) != nullptr;
    case phaseFocus:
        return Find(program, mwin_eventFocusGained, 0) != nullptr;
    case phaseKeys:
    case phaseShift:
        return Find(program, mwin_eventKeyUp, program->phase == phaseShift ? 1 : 0) != nullptr;
    case phaseRepeat:
        return CountRepeats(program) >= 2;
    case phasePointer:
        return Find(program, mwin_eventCursorMoved, 0) != nullptr;
    case phaseClicks:
        return Find(program, mwin_eventButtonUp, 1) != nullptr;
    case phaseWheel:
        return Find(program, mwin_eventWheel, 0) != nullptr;
    case phaseRaw:
        return Find(program, mwin_eventRawPointerDelta, 0) != nullptr;
    default:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    }
}

static bool TextIs(const Program* program, const char* text)
{
    return program->textLength == strlen(text) && memcmp(program->text, text, strlen(text)) == 0;
}

// The pointer's and the cursor's phases.
static void AdvancePointer(Program* program, mwinContext* context)
{
    const mwinEvent* completed = Find(program, mwin_eventRequestCompleted, 0);
    mwinOutcome outcome = completed != nullptr ? completed->data.completion.outcome : 0;
    switch (program->phase)
    {
    case phasePointer:
    {
        const mwinEvent* moved = Find(program, mwin_eventCursorMoved, 0);
        CHECK(moved->data.pointer.position.x == 100.0f && moved->data.pointer.position.y == 120.0f,
              "the pointer moves in the window's units");
        for (int i = 0; i < 2; i++)
        {
            Fake(program, XCB_BUTTON_PRESS, 1, 0, 0);
            Fake(program, XCB_BUTTON_RELEASE, 1, 0, 0);
        }
        break;
    }
    case phaseClicks:
        CHECK(Find(program, mwin_eventButtonDown, 0)->data.pointer.clicks == 1 &&
                  Find(program, mwin_eventButtonDown, 1)->data.pointer.clicks == 2 &&
                  Find(program, mwin_eventButtonDown, 0)->data.pointer.button == mwin_buttonLeft,
              "a quick second click is a double click");
        Fake(program, XCB_BUTTON_PRESS, 4, 0, 0);
        Fake(program, XCB_BUTTON_RELEASE, 4, 0, 0);
        break;
    case phaseWheel:
        CHECK(Find(program, mwin_eventWheel, 0)->data.wheel.y == 1.0f &&
                  Find(program, mwin_eventButtonDown, 0) == nullptr,
              "button 4 turns the wheel away from the user");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a text cursor");
        break;
    case phaseShape:
        CHECK(outcome == mwin_outcomeDone, "the shape from the theme or the cursor font");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorConfinedHidden, nullptr) ==
                  mwin_success,
              "confine and hide");
        break;
    case phaseConfine:
        CHECK(outcome == mwin_outcomeDone && !CanGrab(program),
              "confined: the window holds a grab");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorVisible, nullptr) ==
                  mwin_success,
              "release");
        break;
    case phaseRelease:
        CHECK(outcome == mwin_outcomeDone && CanGrabSoon(program), "released: the grab goes");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorCaptured, nullptr) ==
                  mwin_success,
              "capture");
        break;
    case phaseCapture:
        CHECK(outcome == mwin_outcomeDone && !CanGrab(program), "captured: grabbed and hidden");
        // Relative motion, as a mouse makes it.
        Fake(program, XCB_MOTION_NOTIFY, 1, 5, -3);
        break;
    case phaseRaw:
    {
        const mwinEvent* delta = Find(program, mwin_eventRawPointerDelta, 0);
        CHECK(delta->data.delta.x == 5.0f && delta->data.delta.y == -3.0f,
              "raw motion while captured");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorVisible, nullptr) ==
                  mwin_success,
              "release the capture");
        break;
    }
    case phaseUncapture:
        CHECK(outcome == mwin_outcomeDone && CanGrabSoon(program), "released: the grab goes");
        break;
    default:
        break;
    }
}

// Checks what the phase brought, and starts the next.
static void Advance(Program* program, mwinContext* context)
{
    const mwinEvent* down = Find(program, mwin_eventKeyDown, 0);
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(mwinRequestFocus(context, program->window, nullptr) == mwin_success, "focus");
        break;
    case phaseFocus:
        Fake(program, XCB_KEY_PRESS, KEYCODE_A, 0, 0);
        Fake(program, XCB_KEY_RELEASE, KEYCODE_A, 0, 0);
        break;
    case phaseKeys:
        CHECK(down != nullptr && down->data.key.code == mwin_codeKeyA &&
                  down->data.key.key == 'a' && !down->data.key.repeat && TextIs(program, "a"),
              "a key, what it means, and its text");
        Fake(program, XCB_KEY_PRESS, KEYCODE_SHIFT_L, 0, 0);
        Fake(program, XCB_KEY_PRESS, KEYCODE_A, 0, 0);
        Fake(program, XCB_KEY_RELEASE, KEYCODE_A, 0, 0);
        Fake(program, XCB_KEY_RELEASE, KEYCODE_SHIFT_L, 0, 0);
        break;
    case phaseShift:
    {
        const mwinEvent* a = Find(program, mwin_eventKeyDown, 1);
        CHECK(a != nullptr && a->data.key.key == 'a' &&
                  (a->data.key.modifiers & mwin_modShift) != 0 && TextIs(program, "A"),
              "Shift changes the text, not the meaning");
        Fake(program, XCB_KEY_PRESS, KEYCODE_A, 0, 0);
        break;
    }
    case phaseRepeat:
        CHECK(Find(program, mwin_eventKeyUp, 0) == nullptr && program->textLength >= 3,
              "the X server's repeats, without releases, each typing");
        Fake(program, XCB_KEY_RELEASE, KEYCODE_A, 0, 0);
        Fake(program, XCB_MOTION_NOTIFY, 0, 100, 120);
        break;
    default:
        AdvancePointer(program, context);
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->textLength = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){640.0f, 480.0f};
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
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
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 1000000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        return 77;
    }
    unsetenv("WAYLAND_DISPLAY");
    Program program = {0};
    program.connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(program.connection) != 0)
    {
        return 77;
    }
    program.root = xcb_setup_roots_iterator(xcb_get_setup(program.connection)).data->root;
    // Nothing holds the pointer before the test.
    Fake(&program, XCB_MOTION_NOTIFY, 0, 1000, 700);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the X server");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    xcb_disconnect(program.connection);
    return s_failures == 0 ? 0 : 1;
}
