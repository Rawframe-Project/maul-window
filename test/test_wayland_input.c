// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's keyboard against the test compositor of
// wayland_server.h: keys with their codes, meanings and text, the
// modifiers, repeat, a compose sequence (a dead key), a change of
// layout group with its record and new meanings, and the reset when
// focus goes while a key is held. Skipped (exit status 77) without
// XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"

#include <linux/input-event-codes.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 256
#define TEXT_BYTES  1024

typedef enum Phase
{
    phaseCreate,
    phaseKeys,
    phaseShift,
    phaseRepeat,
    phaseCompose,
    phaseLayout,
    phaseLeave,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    // The records since the phase began; their text is copied.
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[TEXT_BYTES];
    uint32_t textLength;
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
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        if (event.type == mwin_eventTextInput &&
            program->textLength + event.data.text.length <= TEXT_BYTES)
        {
            memcpy(program->text + program->textLength, event.data.text.text,
                   event.data.text.length);
            program->textLength += event.data.text.length;
        }
        program->records[program->count++] = event;
    }
}

static int CountOf(const Program* program, mwinEventType type, bool repeat)
{
    int count = 0;
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        count +=
            event->type == type && (type != mwin_eventKeyDown || event->data.key.repeat == repeat);
    }
    return count;
}

static const mwinEvent* First(const Program* program, mwinEventType type)
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

static bool TextIs(const Program* program, const char* text)
{
    return program->textLength == strlen(text) && memcmp(program->text, text, strlen(text)) == 0;
}

// Whether what the phase waits for has come.
static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return First(program, mwin_eventRequestCompleted) != nullptr;
    case phaseKeys:
    case phaseShift:
    case phaseCompose:
        return CountOf(program, mwin_eventKeyUp, false) >= (program->phase == phaseCompose ? 2 : 1);
    case phaseRepeat:
        return CountOf(program, mwin_eventKeyDown, true) >= 2;
    case phaseLayout:
        return First(program, mwin_eventKeyboardLayoutChanged) != nullptr;
    case phaseLeave:
        return First(program, mwin_eventInputStateReset) != nullptr;
    default:
        return true;
    }
}

static void Press(Server* server, uint32_t evdev)
{
    ServerKey(server, evdev, true);
    ServerKey(server, evdev, false);
}

// Checks what the phase brought, and starts the next.
static void Advance(Program* program, mwinContext* context)
{
    Server* server = program->server;
    const mwinEvent* down = First(program, mwin_eventKeyDown);
    switch (program->phase)
    {
    case phaseCreate:
        ServerEnter(server);
        Press(server, KEY_A);
        break;
    case phaseKeys:
        CHECK(down != nullptr && down->data.key.code == mwin_codeKeyA &&
                  down->data.key.key == 'a' && !down->data.key.repeat &&
                  down->data.key.modifiers == 0 && TextIs(program, "a"),
              "a key, what it means, and its text");
        ServerModifiers(server, 1, 0);
        Press(server, KEY_A);
        break;
    case phaseShift:
        CHECK(down != nullptr && down->data.key.key == 'a' &&
                  (down->data.key.modifiers & mwin_modShift) != 0 && TextIs(program, "A"),
              "Shift changes the text, not the meaning");
        ServerModifiers(server, 0, 0);
        ServerKey(server, KEY_A, true);
        break;
    case phaseRepeat:
    {
        int repeats = CountOf(program, mwin_eventKeyDown, true);
        bool typed = program->textLength == (uint32_t)repeats + 1;
        for (uint32_t i = 0; i < program->textLength; i++)
        {
            typed = typed && program->text[i] == 'a';
        }
        CHECK(repeats >= 2 && typed, "a held key repeats, and each repeat types");
        ServerKey(server, KEY_A, false);
        // us(intl): the apostrophe is a dead acute.
        Press(server, KEY_APOSTROPHE);
        Press(server, KEY_E);
        break;
    }
    case phaseCompose:
        CHECK(TextIs(program, "\xC3\xA9"), "a dead key composes");
        ServerModifiers(server, 0, 1);
        break;
    case phaseLayout:
    {
        char name[64];
        size_t length = 0;
        CHECK(mwinMapKeyCode(context, mwin_codeKeyY) == 'z' &&
                  mwinGetKeyboardLayout(context, name, sizeof(name), &length) == mwin_success &&
                  length >= 6 && memcmp(name, "German", 6) == 0,
              "the second group is German");
        ServerKey(server, KEY_A, true);
        ServerLeave(server);
        break;
    }
    default:
        CHECK(CountOf(program, mwin_eventKeyUp, false) == 0, "no release comes alone");
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
        struct timespec pause = {0, 500000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    Server server;
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&server, "us,de", "intl,"))
    {
        return 77;
    }
    // The compose table of the locale.
    setenv("LANG", "en_US.UTF-8", 1);
    Program program = {.server = &server};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    return s_failures == 0 ? 0 : 1;
}
