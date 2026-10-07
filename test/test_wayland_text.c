// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland input method's edges, against the test compositor of
// wayland_server.h with a text limit of 32 bytes:
// - a composition with its cursor at the start shows the caret there,
//   and commits no text;
// - a commit of exactly the limit arrives whole;
// - a commit past it is lost, with a reset, while the composition
//   beside it arrives;
// - text input turned off ends the composition, and so does the text
//   input leaving the window.
// Skipped (exit status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"
#include "maul-window/input.h"

#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 32
#define LIMIT       32u
// A text of exactly the limit, and one byte more.
#define EXACT "0123456789abcdef0123456789abcdef"
#define PAST  EXACT "!"

typedef enum Phase
{
    phaseCreate,
    phaseEnabled,
    phaseStart,
    phaseExact,
    phasePast,
    phaseDisabled,
    phaseEnabledAgain,
    phaseComposing,
    phaseLeft,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinEvent records[MAX_RECORDS];
    // The text of the records' text input, kept while the frame runs.
    char text[2 * LIMIT];
    uint32_t textLength;
    int count;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
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

static void Enable(mwinContext* context, const Program* program, bool enabled)
{
    CHECK(mwinRequestTextInput(context, program->window, enabled,
                               (mwinRect){10.0f, 20.0f, 1.0f, 16.0f}, nullptr) == mwin_success,
          "text input");
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventWindowCreated) != nullptr;
    case phaseEnabled:
    case phaseEnabledAgain:
        return ServerText(program->server).enabled;
    case phaseExact:
        return Find(program, mwin_eventTextInput) != nullptr;
    case phasePast:
        return Find(program, mwin_eventInputStateReset) != nullptr &&
               Find(program, mwin_eventImePreedit) != nullptr;
    default:
        return Find(program, mwin_eventImePreedit) != nullptr;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    Server* server = program->server;
    const mwinEvent* preedit = Find(program, mwin_eventImePreedit);
    switch (program->phase)
    {
    case phaseCreate:
        // The keyboard's focus, which a composition's window has.
        ServerEnter(server);
        ServerTextEnter(server);
        Enable(context, program, true);
        break;
    case phaseEnabled:
        ServerCompose(server, "ab", 0, 0, nullptr);
        break;
    case phaseStart:
        CHECK(preedit->data.preedit.length == 2 && preedit->data.preedit.caret == 0 &&
                  preedit->data.preedit.selectionStart == 0 &&
                  preedit->data.preedit.selectionEnd == 0 &&
                  Find(program, mwin_eventTextInput) == nullptr,
              "a composition with its cursor at the start, and no text");
        ServerCompose(server, nullptr, 0, 0, EXACT);
        break;
    case phaseExact:
        CHECK(program->textLength == LIMIT && memcmp(program->text, EXACT, LIMIT) == 0 &&
                  Find(program, mwin_eventInputStateReset) == nullptr,
              "a commit of exactly the limit arrives whole");
        ServerCompose(server, "x", 1, 1, PAST);
        break;
    case phasePast:
        CHECK(program->textLength == 0 && preedit->data.preedit.length == 1,
              "a commit past the limit lost, with a reset, the composition beside it kept");
        Enable(context, program, false);
        break;
    case phaseDisabled:
        CHECK(preedit->data.preedit.length == 0 && preedit->data.preedit.caret == -1,
              "text input turned off ends the composition");
        Enable(context, program, true);
        break;
    case phaseEnabledAgain:
        ServerCompose(server, "y", 1, 1, nullptr);
        break;
    case phaseComposing:
        ServerTextLeave(server);
        break;
    default:
        CHECK(preedit->data.preedit.length == 0 && preedit->data.preedit.caret == -1,
              "the text input leaving the window ends the composition");
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

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    static Server server;
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&server, "us", ""))
    {
        return 77;
    }
    static Program program;
    program = (Program){.server = &server};
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.textBytesPerWindow = LIMIT;
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    return s_failures == 0 ? 0 : 1;
}
