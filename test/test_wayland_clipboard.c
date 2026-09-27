// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's clipboard against the test compositor of
// wayland_server.h and its data device: a write sets the selection with
// every text type, quoting the serial of the keyboard's focus, and
// serves another client's read; a read while the program owns the
// selection answers from its own text; a text larger than a pipe holds
// is served a piece at each pump; a reader that closes its pipe at once
// costs nothing (SIGPIPE would end the test); another client's text is
// read by its UTF-8 type, repaired; text past the limit is too large;
// no selection reads as empty. Skipped (exit status 77) without
// XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_data_server.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"

#include <signal.h>
#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   50000000ull
#define LIMIT       (256u * 1024u)
#define LARGE       (200u * 1024u)

typedef enum Phase
{
    phaseCreate,
    phaseFocus,
    phaseWrite,
    phaseServe,
    phaseOwnRead,
    phaseLargeWrite,
    phaseLargeServe,
    phaseClosedReader,
    phaseOffered,
    phaseTooLarge,
    phaseNothing,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    DataServer* data;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
    // The request's outcome once answered, or -1.
    int outcome;
    bool created;
    // The phase's read was asked for, after the client heard of the
    // change.
    bool reading;
    // Another client's read of the selection: its pipe, and what came.
    int fd;
    char* got;
    size_t gotLength;
    bool gotAll;
    // The serial the keyboard's focus came with.
    uint32_t focusSerial;
    char* large;
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
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        program->created = program->created || event.type == mwin_eventWindowCreated;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == program->request.index1 &&
            completion->request.generation == program->request.generation)
        {
            program->outcome = completion->outcome;
        }
    }
}

// Reads what another client's pipe has.
static void Receive(Program* program)
{
    char piece[4096];
    ssize_t got = 0;
    while (program->fd >= 0 && (got = read(program->fd, piece, sizeof(piece))) > 0)
    {
        memcpy(program->got + program->gotLength, piece, (size_t)got);
        program->gotLength += (size_t)got;
    }
    if (program->fd >= 0 && got == 0)
    {
        close(program->fd);
        program->fd = -1;
        program->gotAll = true;
    }
}

static void Request(Program* program)
{
    program->gotLength = 0;
    program->gotAll = false;
    program->fd = DataRequest(program->data);
}

static bool Found(mwinContext* context, const char* expected, size_t length)
{
    char text[64];
    size_t found = 0;
    return mwinGetClipboardText(context, text, sizeof(text), &found) == mwin_success &&
           found == length && memcmp(text, expected, length) == 0;
}

static void Write(Program* program, mwinContext* context, const char* text, size_t length)
{
    CHECK(mwinRequestClipboardWrite(context, program->window, text, length, &program->request) ==
              mwin_success,
          "a write");
}

static void Read(Program* program, mwinContext* context)
{
    CHECK(mwinRequestClipboardRead(context, program->window, &program->request) == mwin_success,
          "a read");
}

// Whether the phase is done: its request answered, the other client's
// read finished, or, after a change of selection the client must hear
// of first, its read answered.
static bool Ready(Program* program, mwinContext* context)
{
    bool settled = NowNs() - program->startNs >= SETTLE_NS;
    switch (program->phase)
    {
    case phaseCreate:
        return program->created;
    case phaseServe:
    case phaseLargeServe:
        Receive(program);
        return program->gotAll;
    case phaseWrite:
    case phaseLargeWrite:
    {
        // The compositor has the selection the write set.
        uint32_t serial = 0;
        bool types = false;
        int selections = DataSelection(program->data, &serial, &types);
        return program->outcome >= 0 && selections == (program->phase == phaseWrite ? 1 : 2);
    }
    case phaseFocus:
    case phaseClosedReader:
        return settled;
    case phaseOffered:
    case phaseTooLarge:
    case phaseNothing:
        if (settled && !program->reading)
        {
            Read(program, context);
            program->reading = true;
        }
        return program->reading && program->outcome >= 0;
    default:
        return program->outcome >= 0;
    }
}

static void AdvanceWrites(Program* program, mwinContext* context, int outcome)
{
    switch (program->phase)
    {
    case phaseCreate:
        ServerEnter(program->server);
        // The enter event's serial; the modifiers event followed it.
        program->focusSerial = program->server->serial - 1;
        break;
    case phaseFocus:
        Write(program, context, "h\xC3\xA9llo", 6);
        break;
    case phaseWrite:
    {
        uint32_t serial = 0;
        bool types = false;
        CHECK(outcome == mwin_outcomeDone && DataSelection(program->data, &serial, &types) == 1 &&
                  serial == program->focusSerial && types,
              "the selection set with every text type, quoting the focus's serial");
        Request(program);
        break;
    }
    case phaseServe:
        CHECK(program->gotLength == 6 && memcmp(program->got, "h\xC3\xA9llo", 6) == 0,
              "another client reads the text");
        Read(program, context);
        break;
    case phaseOwnRead:
        CHECK(outcome == mwin_outcomeDone && Found(context, "h\xC3\xA9llo", 6),
              "a read of the program's own selection");
        Write(program, context, program->large, LARGE);
        break;
    case phaseLargeWrite:
        CHECK(outcome == mwin_outcomeDone, "a large write");
        Request(program);
        break;
    default:
        CHECK(program->gotLength == LARGE && memcmp(program->got, program->large, LARGE) == 0,
              "a text larger than a pipe holds, served a piece at a time");
        // The reader goes before the client writes.
        close(DataRequest(program->data));
        break;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    static char s_large[LIMIT + 1];
    // The phase's checks read the outcome before its action asks anew.
    int outcome = program->outcome;
    program->outcome = -1;
    program->reading = false;
    switch (program->phase)
    {
    case phaseClosedReader:
        CHECK(true, "a reader that went costs nothing");
        DataOffer(program->data, "A\xC3(", 3);
        break;
    case phaseOffered:
        CHECK(outcome == mwin_outcomeDone && Found(context, "A\xEF\xBF\xBD(", 5) &&
                  DataReceived(program->data, "text/plain;charset=utf-8"),
              "another client's text read by its UTF-8 type, repaired");
        memset(s_large, 'x', sizeof(s_large));
        DataOffer(program->data, s_large, sizeof(s_large));
        break;
    case phaseTooLarge:
        CHECK(outcome == mwin_outcomeTooLarge, "text past the limit too large");
        DataOffer(program->data, nullptr, 0);
        break;
    case phaseNothing:
        CHECK(outcome == mwin_outcomeDone && Found(context, "", 0), "no selection read as empty");
        break;
    default:
        AdvanceWrites(program, context, outcome);
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
    Collect(program, context);
    if (Ready(program, context))
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
    static DataServer data;
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&server, "us", ""))
    {
        return 77;
    }
    DataStart(&data, &server);
    // A shell may leave SIGPIPE ignored; the test needs it to kill.
    (void)signal(SIGPIPE, SIG_DFL);
    static char s_got[LARGE + 1];
    static char s_largeText[LARGE];
    for (size_t i = 0; i < LARGE; i++)
    {
        s_largeText[i] = (char)('a' + i % 26);
    }
    Program program = {.server = &server, .data = &data, .outcome = -1, .fd = -1};
    program.got = s_got;
    program.large = s_largeText;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.clipboardBytes = LIMIT;
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    DataStop(&data);
    return s_failures == 0 ? 0 : 1;
}
