// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's clipboard data and primary selection
// (mwin-0029) against the test compositor of wayland_server.h: a data
// write's source offers each MIME type beside the text types, serves
// another client each item, a large one a piece at each pump, and closes
// a type it lacks; a data read while the program owns the selection
// answers from its own data, and fails for a type it lacks; data alone
// offers no text; a primary write sets the primary selection's source
// apart from the clipboard. Once another client offers them, a text read
// and a data read made together are both answered, one after the other;
// a large item is read; the primary selection's text is read; and a data
// read with no selection fails. Skipped (exit status 77) without
// XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_primary_server.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"

#include <signal.h>
#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   50000000ull
#define LIMIT       (256u * 1024u)
#define LARGE       (200u * 1024u)

#define PNG  "image/png"
#define BIG  "application/x-maul-big"
#define UTF8 "text/plain;charset=utf-8"

static const char s_png[] = {'\x89', 'P', 'N', 'G', '\0', '\r', '\n'};
static const char s_peerPng[] = {'p', 'e', 'e', 'r', '\0', 'P', 'N', 'G'};

typedef enum Phase
{
    phaseCreate,
    phaseFocus,
    phaseWrite,
    phaseServeData,
    phaseServeLarge,
    phaseServeText,
    phaseServeMissing,
    phaseOwnData,
    phaseOwnMissing,
    phaseDataOnly,
    phasePrimaryWrite,
    phaseServePrimary,
    phaseOtherBoth,
    phaseOtherLarge,
    phaseOtherPrimary,
    phaseNothing,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    DataServer* data;
    PrimaryServer* primary;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    // The phase's request, and in one phase a second made with it, with
    // their outcomes once answered, or -1.
    mwinRequestId request;
    mwinRequestId second;
    int outcome;
    int secondOutcome;
    bool created;
    // The phase's reads were asked for, after the client heard of the
    // change.
    bool reading;
    // Another client's read: its pipe, and what came.
    int fd;
    char* got;
    size_t gotLength;
    bool gotAll;
    char* large;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static bool Same(mwinRequestId a, mwinRequestId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        program->created = program->created || event.type == mwin_eventWindowCreated;
        if (event.type == mwin_eventRequestCompleted && Same(completion->request, program->request))
        {
            program->outcome = completion->outcome;
        }
        if (event.type == mwin_eventRequestCompleted && Same(completion->request, program->second))
        {
            program->secondOutcome = completion->outcome;
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

// Another client reads the clipboard as a type, or the primary
// selection with none.
static void Request(Program* program, const char* type)
{
    program->gotLength = 0;
    program->gotAll = false;
    program->fd =
        type != nullptr ? DataRequestAs(program->data, type) : PrimaryRequest(program->primary);
}

static bool Got(const Program* program, const char* expected, size_t length)
{
    return program->gotLength == length && memcmp(program->got, expected, length) == 0;
}

static bool FoundData(mwinContext* context, const char* expected, size_t length)
{
    static char s_data[LIMIT];
    size_t found = 0;
    return mwinGetClipboardData(context, s_data, sizeof(s_data), &found) == mwin_success &&
           found == length && memcmp(s_data, expected, length) == 0;
}

static bool FoundText(mwinContext* context, bool primary, const char* expected, size_t length)
{
    char text[64];
    size_t found = 0;
    mwinResult status = primary ? mwinGetPrimaryText(context, text, sizeof(text), &found)
                                : mwinGetClipboardText(context, text, sizeof(text), &found);
    return status == mwin_success && found == length && memcmp(text, expected, length) == 0;
}

static void ReadData(Program* program, mwinContext* context, const char* mime,
                     mwinRequestId* requestOut)
{
    CHECK(mwinRequestClipboardReadData(context, program->window, mime, strlen(mime), requestOut) ==
              mwin_success,
          "a data read");
}

// The reads of the phases that wait for the client to hear of another
// client's selection first.
static void StartReads(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseOtherBoth:
        CHECK(mwinRequestClipboardRead(context, program->window, &program->request) == mwin_success,
              "a text read");
        ReadData(program, context, PNG, &program->second);
        break;
    case phaseOtherLarge:
        ReadData(program, context, BIG, &program->request);
        break;
    case phaseOtherPrimary:
        CHECK(mwinRequestPrimaryRead(context, program->window, &program->request) == mwin_success,
              "a primary read");
        break;
    default:
        ReadData(program, context, PNG, &program->request);
        break;
    }
}

// Whether the phase is done: its requests answered (a write's once the
// compositor has its source), the other client's read finished, or, after another client's
// selection the client must hear of first, its reads answered.
static bool Ready(Program* program, mwinContext* context)
{
    bool settled = NowNs() - program->startNs >= SETTLE_NS;
    switch (program->phase)
    {
    case phaseCreate:
        return program->created;
    case phaseFocus:
        return settled;
    case phaseServeData:
    case phaseServeLarge:
    case phaseServeText:
    case phaseServeMissing:
    case phaseServePrimary:
        Receive(program);
        return program->gotAll;
    case phaseOtherBoth:
    case phaseOtherLarge:
    case phaseOtherPrimary:
    case phaseNothing:
        if (settled && !program->reading)
        {
            StartReads(program, context);
            program->reading = true;
        }
        return program->reading && program->outcome >= 0 &&
               (program->phase != phaseOtherBoth || program->secondOutcome >= 0);
    case phaseWrite:
    case phaseDataOnly:
    {
        // The compositor has the selection the write set.
        uint32_t serial = 0;
        bool types = false;
        int selections = DataSelection(program->data, &serial, &types);
        return program->outcome >= 0 && selections == (program->phase == phaseWrite ? 1 : 2);
    }
    case phasePrimaryWrite:
        return program->outcome >= 0 && PrimarySourceHas(program->primary, UTF8);
    default:
        return program->outcome >= 0;
    }
}

static void Write(Program* program, mwinContext* context)
{
    mwinClipboardItem items[] = {
        {PNG, strlen(PNG), s_png, sizeof(s_png)},
        {"text/plain", 10, "hi", 2},
        {BIG, strlen(BIG), program->large, LARGE},
    };
    CHECK(mwinRequestClipboardWriteData(context, program->window, items, 3, &program->request) ==
              mwin_success,
          "a data write");
}

// The phases where the program owns the selections.
static void AdvanceOwned(Program* program, mwinContext* context, int outcome)
{
    DataServer* data = program->data;
    switch (program->phase)
    {
    case phaseCreate:
        ServerEnter(program->server);
        break;
    case phaseFocus:
        Write(program, context);
        break;
    case phaseWrite:
        CHECK(outcome == mwin_outcomeDone && DataSourceHas(data, PNG) && DataSourceHas(data, BIG) &&
                  DataSourceHas(data, UTF8),
              "the source offers each type and the text");
        Request(program, PNG);
        break;
    case phaseServeData:
        CHECK(Got(program, s_png, sizeof(s_png)), "another client reads an item");
        Request(program, BIG);
        break;
    case phaseServeLarge:
        CHECK(Got(program, program->large, LARGE), "a large item served a piece at a time");
        Request(program, UTF8);
        break;
    case phaseServeText:
        CHECK(Got(program, "hi", 2), "the write's text is the clipboard's text");
        Request(program, "image/gif");
        break;
    case phaseServeMissing:
        CHECK(program->gotLength == 0, "a type the source lacks gets nothing");
        ReadData(program, context, PNG, &program->request);
        break;
    case phaseOwnData:
        CHECK(outcome == mwin_outcomeDone && FoundData(context, s_png, sizeof(s_png)),
              "a data read of the program's own data");
        ReadData(program, context, "image/gif", &program->request);
        break;
    case phaseOwnMissing:
    {
        CHECK(outcome == mwin_outcomeFailed, "a type the data lacks fails");
        mwinClipboardItem item = {PNG, strlen(PNG), s_png, sizeof(s_png)};
        CHECK(mwinRequestClipboardWriteData(context, program->window, &item, 1,
                                            &program->request) == mwin_success,
              "a write of data alone");
        break;
    }
    case phaseDataOnly:
        CHECK(outcome == mwin_outcomeDone && DataSourceHas(data, PNG) && !DataSourceHas(data, UTF8),
              "data alone offers no text");
        CHECK(mwinRequestPrimaryWrite(context, program->window, "sel", 3, &program->request) ==
                  mwin_success,
              "a primary write");
        break;
    default:
        CHECK(outcome == mwin_outcomeDone && PrimarySourceHas(program->primary, UTF8) &&
                  DataSourceHas(data, PNG),
              "the primary selection's source, apart from the clipboard's");
        Request(program, nullptr);
        break;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    static const char* const s_pngType[] = {PNG};
    static const char* const s_bigType[] = {BIG};
    int outcome = program->outcome;
    int second = program->secondOutcome;
    program->outcome = -1;
    program->secondOutcome = -1;
    program->reading = false;
    switch (program->phase)
    {
    case phaseServePrimary:
        CHECK(Got(program, "sel", 3), "another client reads the primary selection");
        DataOfferTypes(program->data, s_pngType, 1, s_peerPng, sizeof(s_peerPng));
        break;
    case phaseOtherBoth:
        CHECK(outcome == mwin_outcomeDone && FoundText(context, false, "", 0),
              "a text read of data without text is empty");
        CHECK(second == mwin_outcomeDone && FoundData(context, s_peerPng, sizeof(s_peerPng)),
              "a data read made with it is answered after it");
        DataOfferTypes(program->data, s_bigType, 1, program->large, LARGE);
        break;
    case phaseOtherLarge:
        CHECK(outcome == mwin_outcomeDone && FoundData(context, program->large, LARGE),
              "another client's large item read");
        PrimaryOffer(program->primary, "peer sel", 8);
        break;
    case phaseOtherPrimary:
        CHECK(outcome == mwin_outcomeDone && FoundText(context, true, "peer sel", 8),
              "another client's primary selection read");
        DataOffer(program->data, nullptr, 0);
        break;
    case phaseNothing:
        CHECK(outcome == mwin_outcomeFailed, "a data read with no selection fails");
        break;
    default:
        AdvanceOwned(program, context, outcome);
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
    static PrimaryServer primary;
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&server, "us", ""))
    {
        return 77;
    }
    DataStart(&data, &server);
    PrimaryStart(&primary, &server);
    // A shell may leave SIGPIPE ignored; the test needs it to kill.
    (void)signal(SIGPIPE, SIG_DFL);
    static char s_got[LARGE + 1];
    static char s_large[LARGE];
    for (size_t i = 0; i < LARGE; i++)
    {
        s_large[i] = (char)(i % 251);
    }
    Program program = {.server = &server,
                       .data = &data,
                       .primary = &primary,
                       .outcome = -1,
                       .secondOutcome = -1,
                       .fd = -1,
                       .got = s_got,
                       .large = s_large};
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
    PrimaryStop(&primary);
    return s_failures == 0 ? 0 : 1;
}
