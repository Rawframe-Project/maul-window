// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's input against a real X server (Xvfb in CI), driven
// through XTEST from a connection of the test's own: keys with their
// codes, meanings and text, Shift, the X server's repeats, the pointer
// entering and moving, a double click with the buttons held, the
// wheel's buttons up and to the sides, the back button and a tenth one
// left out, cursor shapes other than the default, a cursor made from
// images as XFixes reads it back, a hidden cursor, a confined one whose
// grab another client then meets and that left the pointer where it
// was, and a captured one kept in the middle, with XInput 2's raw
// motion. Without DISPLAY the test is skipped (exit status 77).

#include "cursor_images.h"
#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
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
    phaseSideWheel,
    phaseBack,
    phaseShape,
    phaseImage,
    phaseShapeAgain,
    phaseImageAgain,
    phaseDestroyed,
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
    mwinCursorId cursor;
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[64];
    uint32_t textLength;
    // The size and hotspot of the cursor the window shows by default.
    uint64_t defaultGlyph;
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

// Whether the X server shows the red image, by XFixes.
// The size and hotspot of the cursor shown.
static uint64_t Glyph(const Program* program)
{
    xcb_xfixes_get_cursor_image_reply_t* reply = xcb_xfixes_get_cursor_image_reply(
        program->connection, xcb_xfixes_get_cursor_image(program->connection), nullptr);
    uint64_t glyph = reply != nullptr
                         ? (uint64_t)reply->width << 48 | (uint64_t)reply->height << 32 |
                               (uint64_t)reply->xhot << 16 | reply->yhot
                         : 0;
    free(reply);
    return glyph;
}

// Whether the cursor shown is another than the default, soon: the X
// server takes the program's requests and the test's in either order.
static bool ShowsOtherSoon(const Program* program)
{
    for (int i = 0; i < 100; i++)
    {
        if (Glyph(program) != program->defaultGlyph)
        {
            return true;
        }
        struct timespec pause = {0, 5000000};
        (void)nanosleep(&pause, nullptr);
    }
    return false;
}

// Whether the pointer is at a place of the root window, soon.
static bool PointerAtSoon(const Program* program, int16_t x, int16_t y)
{
    for (int i = 0; i < 100; i++)
    {
        xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(
            program->connection, xcb_query_pointer(program->connection, program->root), nullptr);
        bool there = pointer != nullptr && pointer->root_x == x && pointer->root_y == y;
        free(pointer);
        if (there)
        {
            return true;
        }
        struct timespec pause = {0, 5000000};
        (void)nanosleep(&pause, nullptr);
    }
    return false;
}

static bool ShowsRed(Program* program)
{
    xcb_xfixes_get_cursor_image_reply_t* reply = xcb_xfixes_get_cursor_image_reply(
        program->connection, xcb_xfixes_get_cursor_image(program->connection), nullptr);
    bool red = reply != nullptr && reply->width == 16 && reply->height == 16 && reply->xhot == 3 &&
               reply->yhot == 5 &&
               xcb_xfixes_get_cursor_image_cursor_image(reply)[0] == 0xFFFF0000u;
    free(reply);
    return red;
}

// Whether the X server comes to show the red image, or not, within half
// a second: the program's requests and the test's go apart.
static bool ShowsRedSoon(Program* program, bool red)
{
    for (int i = 0; i < 100; i++)
    {
        if (ShowsRed(program) == red)
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
    case phaseSideWheel:
        return Find(program, mwin_eventWheel, 1) != nullptr;
    case phaseBack:
        return Find(program, mwin_eventButtonUp, 0) != nullptr;
    case phaseRaw:
        return Find(program, mwin_eventRawPointerDelta, 0) != nullptr;
    case phaseDestroyed:
        // Frames have run, and the backend has sent what it does.
        return NowNs() - program->startNs > 50000000u;
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
        CHECK(Find(program, mwin_eventCursorEntered, 0) != nullptr, "the pointer enters");
        program->defaultGlyph = Glyph(program);
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
        CHECK(Find(program, mwin_eventButtonDown, 0)->data.pointer.buttons == 1 &&
                  Find(program, mwin_eventButtonUp, 0)->data.pointer.buttons == 0,
              "the left button held while down");
        Fake(program, XCB_BUTTON_PRESS, 4, 0, 0);
        Fake(program, XCB_BUTTON_RELEASE, 4, 0, 0);
        break;
    case phaseWheel:
        CHECK(Find(program, mwin_eventWheel, 0)->data.wheel.y == 1.0f &&
                  Find(program, mwin_eventButtonDown, 0) == nullptr,
              "button 4 turns the wheel away from the user");
        for (uint8_t button = 6; button <= 7; button++)
        {
            Fake(program, XCB_BUTTON_PRESS, button, 0, 0);
            Fake(program, XCB_BUTTON_RELEASE, button, 0, 0);
        }
        break;
    case phaseSideWheel:
    {
        const mwinWheelEvent* left = &Find(program, mwin_eventWheel, 0)->data.wheel;
        const mwinWheelEvent* right = &Find(program, mwin_eventWheel, 1)->data.wheel;
        CHECK(left->x == -1.0f && left->y == 0.0f && right->x == 1.0f && right->y == 0.0f,
              "buttons 6 and 7 turn it left and right");
        // The X server's pointer has ten buttons: the tenth is no one's.
        for (uint8_t button = 10; button >= 8; button -= 2)
        {
            Fake(program, XCB_BUTTON_PRESS, button, 0, 0);
            Fake(program, XCB_BUTTON_RELEASE, button, 0, 0);
        }
        break;
    }
    case phaseBack:
        CHECK(Find(program, mwin_eventButtonDown, 0)->data.pointer.button == mwin_buttonBack &&
                  Find(program, mwin_eventButtonDown, 0)->data.pointer.buttons ==
                      1u << (mwin_buttonBack - 1) &&
                  Find(program, mwin_eventButtonDown, 1) == nullptr,
              "button 8 the back button, and the tenth left out");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a text cursor");
        break;
    case phaseShape:
    {
        CHECK(outcome == mwin_outcomeDone && ShowsOtherSoon(program),
              "the shape from the theme or the cursor font");
        mwinIconImage images[2];
        mwinCursorDef def = CursorImagesDef(images);
        CHECK(mwinCreateCursor(context, &def, &program->cursor) == mwin_success &&
                  mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                      mwin_success,
              "a cursor made from images");
        break;
    }
    case phaseImage:
        CHECK(outcome == mwin_outcomeDone && ShowsRedSoon(program, true),
              "the image for scale 1, with its hotspot");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a shape again");
        break;
    case phaseShapeAgain:
        CHECK(outcome == mwin_outcomeDone && ShowsRedSoon(program, false),
              "the shape in the image's place");
        CHECK(mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                  mwin_success,
              "the image again");
        break;
    case phaseImageAgain:
        CHECK(outcome == mwin_outcomeDone && ShowsRedSoon(program, true),
              "the image in the shape's place");
        CHECK(mwinDestroyCursor(context, program->cursor) == mwin_success, "destroyed");
        break;
    case phaseDestroyed:
        CHECK(ShowsRedSoon(program, false), "destroyed, the window shows the default shape");
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
        // Requests sent after the confinement's have been taken.
        CHECK(PointerAtSoon(program, 100, 120), "confined, the pointer stayed where it was");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorCaptured, nullptr) ==
                  mwin_success,
              "capture");
        break;
    case phaseCapture:
        CHECK(outcome == mwin_outcomeDone && !CanGrab(program), "captured: grabbed and hidden");
        CHECK(PointerAtSoon(program, 320, 240), "captured, the pointer kept in the middle");
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
        (void)printf("timed out in phase %d\n", (int)program->phase);
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
    // XFixes answers only a client that told its version.
    free(xcb_xfixes_query_version_reply(
        program.connection, xcb_xfixes_query_version(program.connection, 4, 0), nullptr));
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
