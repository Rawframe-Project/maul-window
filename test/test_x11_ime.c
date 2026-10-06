// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's input methods (mwin-0030) against stand-ins for Fcitx 5
// and IBus on a session bus of the test's own (linux_ime_fake.h), keys
// driven through XTEST: text input enabled makes an input context and
// gives it the focus and the caret in root coordinates; "a" taken by the
// method shows a composition and no key; Return commits it as text; "b",
// which the method leaves, arrives as a key with its text; text input
// disabled resets the context and takes its focus away. It runs once
// with XMODIFIERS naming Fcitx and once naming IBus. Skipped (exit status
// 77) without DISPLAY or dbus-daemon.

#include "linux_ime_fake.h"
#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 256
// X11 key codes on a US layout: evdev codes plus 8.
#define KEYCODE_A      38
#define KEYCODE_B      56
#define KEYCODE_RETURN 36

typedef enum Phase
{
    phaseCreate,
    phaseFocus,
    phaseContext,
    phasePreedit,
    phaseCommit,
    phasePassed,
    phaseDisabled,
    phaseDone,
} Phase;

typedef struct Program
{
    xcb_connection_t* connection;
    xcb_window_t root;
    FakeBus* bus;
    // The framework the run expects: 1 Fcitx, 2 IBus.
    int framework;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[64];
    uint32_t textLength;
    char preedit[64];
    uint32_t preeditLength;
    int32_t preeditCaret;
    uint32_t segments;
    int preedits;
    int ends;
    bool timedOut;
} Program;

static const mwinRect s_caret = {10.0f, 20.0f, 2.0f, 16.0f};

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Press(Program* program, uint8_t keycode)
{
    xcb_test_fake_input(program->connection, XCB_KEY_PRESS, keycode, XCB_CURRENT_TIME,
                        program->root, 0, 0, 0);
    xcb_test_fake_input(program->connection, XCB_KEY_RELEASE, keycode, XCB_CURRENT_TIME,
                        program->root, 0, 0, 0);
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
        if (event.type == mwin_eventImePreedit && event.data.preedit.length == 0)
        {
            program->ends += 1;
        }
        else if (event.type == mwin_eventImePreedit &&
                 event.data.preedit.length <= sizeof(program->preedit))
        {
            program->preedits += 1;
            memcpy(program->preedit, event.data.preedit.text, event.data.preedit.length);
            program->preeditLength = event.data.preedit.length;
            program->preeditCaret = event.data.preedit.caret;
            program->segments = event.data.preedit.segmentCount;
        }
        program->records[program->count++] = event;
    }
}

static int CountKeys(const Program* program, mwinKeyCode code)
{
    int keys = 0;
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* record = &program->records[i];
        keys += (record->type == mwin_eventKeyDown || record->type == mwin_eventKeyUp) &&
                record->data.key.code == code;
    }
    return keys;
}

static bool Has(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type)
        {
            return true;
        }
    }
    return false;
}

// Where the caret is on the screen, as the input method should be told.
static bool CaretOnScreen(Program* program, mwinContext* context, int32_t* xOut, int32_t* yOut)
{
    mwinNativeHandles handles;
    if (mwinGetNativeHandles(context, program->window, &handles) != mwin_success)
    {
        return false;
    }
    xcb_translate_coordinates_reply_t* reply = xcb_translate_coordinates_reply(
        program->connection,
        xcb_translate_coordinates(program->connection, handles.handles.x11.window, program->root,
                                  (int16_t)s_caret.x, (int16_t)s_caret.y),
        nullptr);
    if (reply == nullptr)
    {
        return false;
    }
    *xOut = reply->dst_x;
    *yOut = reply->dst_y;
    free(reply);
    return true;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Has(program, mwin_eventShown);
    case phaseFocus:
        return Has(program, mwin_eventFocusGained);
    case phaseContext:
        return s_fakeIme.made != 0 && s_fakeIme.focusIns > 0 && s_fakeIme.caret[3] == 16;
    case phasePreedit:
        return program->preedits > 0;
    case phaseCommit:
        return program->textLength > 0 && program->ends > 0;
    case phasePassed:
        return CountKeys(program, mwin_codeKeyB) >= 2;
    case phaseDisabled:
        return s_fakeIme.focusOuts > 0;
    default:
        return false;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    bool fcitx = program->framework == 1;
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(mwinRequestFocus(context, program->window, nullptr) == mwin_success, "focus");
        break;
    case phaseFocus:
        CHECK(mwinRequestTextInput(context, program->window, true, s_caret, nullptr) ==
                  mwin_success,
              "text input asked for");
        break;
    case phaseContext:
    {
        int32_t x = 0;
        int32_t y = 0;
        CHECK(s_fakeIme.made == program->framework,
              fcitx ? "Fcitx makes the context" : "IBus makes the context");
        CHECK(CaretOnScreen(program, context, &x, &y) && s_fakeIme.caret[0] == x &&
                  s_fakeIme.caret[1] == y && s_fakeIme.caret[2] == 2,
              "the caret in root coordinates");
        CHECK(s_fakeIme.capabilities != 0, "a composition the program shows asked for");
        Press(program, KEYCODE_A);
        break;
    }
    case phasePreedit:
        CHECK(program->preeditLength == 3 && memcmp(program->preedit, IME_KANA, 3) == 0 &&
                  program->preeditCaret == 3 && program->segments > 0,
              "a composition, its caret and its segments");
        Press(program, KEYCODE_RETURN);
        break;
    case phaseCommit:
        CHECK(program->textLength == 3 && memcmp(program->text, IME_KANA, 3) == 0,
              "the composition committed as text");
        CHECK(CountKeys(program, mwin_codeKeyA) == 0 && CountKeys(program, mwin_codeEnter) == 0,
              "no key the method took");
        CHECK(s_fakeIme.keycode == (fcitx ? KEYCODE_RETURN : KEYCODE_RETURN - 8u),
              "the key's code as the framework takes it");
        Press(program, KEYCODE_B);
        break;
    case phasePassed:
        CHECK(program->textLength == 4 && program->text[3] == 'b',
              "a key the method left arrives with its text");
        CHECK(mwinRequestTextInput(context, program->window, false, s_caret, nullptr) ==
                  mwin_success,
              "text input given up");
        break;
    default:
        CHECK(s_fakeIme.resets > 0, "the context reset");
        break;
    }
    program->phase += 1;
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
    FakePump(program->bus);
    Collect(program, context);
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d of run %d\n", (int)program->phase, program->framework);
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

// One run with XMODIFIERS naming a framework.
static void Run(xcb_connection_t* connection, FakeBus* bus, int framework)
{
    (void)setenv("XMODIFIERS", framework == 1 ? "@im=fcitx" : "@im=ibus", 1);
    s_fakeIme = (FakeIme){0};
    static Program program;
    program = (Program){.connection = connection,
                        .root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root,
                        .bus = bus,
                        .framework = framework};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the X server");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0' || access("/usr/bin/dbus-daemon", X_OK) != 0)
    {
        return 77;
    }
    unsetenv("WAYLAND_DISPLAY");
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection) != 0)
    {
        return 77;
    }
    char directory[] = "/tmp/mwin-ime-XXXXXX";
    static FakeBus bus;
    if (mkdtemp(directory) == nullptr || !FakeStart(&bus, directory) || !FakeImeStart(&bus))
    {
        FakeStop(&bus);
        return 77;
    }
    Run(connection, &bus, 1);
    Run(connection, &bus, 2);
    FakeStop(&bus);
    FakeClean(directory);
    (void)rmdir(directory);
    xcb_disconnect(connection);
    return s_failures == 0 ? 0 : 1;
}
