// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's clipboard data and primary selection (mwin-0029)
// against another client: a data write offers each MIME type as its own
// target beside its text, a large item in pieces, all named in TARGETS;
// a data read while the program owns CLIPBOARD answers from its own
// data, and fails for a type it lacks; data alone offers no text; a
// primary write takes PRIMARY
// apart from CLIPBOARD. Once the other client owns them, a text read and
// a data read made together are both answered, one after the other; a
// large item comes in pieces; PRIMARY's text is read; and a data read
// with no owner fails. Skipped (exit status 77) without DISPLAY.

#include "test_harness.h"
#include "x11_peer.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   50000000ull
#define LIMIT       (256u * 1024u)
#define LARGE       (100u * 1024u)

#define PNG "image/png"
#define BIG "application/x-maul-big"

static const char s_png[] = {'\x89', 'P', 'N', 'G', '\0', '\r', '\n'};
static const char s_peerPng[] = {'p', 'e', 'e', 'r', '\0', 'P', 'N', 'G'};

typedef enum Phase
{
    phaseCreate,
    phaseWrite,
    phasePeerData,
    phaseTargets,
    phasePeerPieces,
    phasePeerText,
    phaseOwnData,
    phaseOwnMissing,
    phaseDataOnly,
    phasePeerRefused,
    phasePrimaryWrite,
    phasePeerPrimary,
    phaseOtherBoth,
    phaseOtherPieces,
    phaseOtherPrimary,
    phaseNoOwner,
    phaseDone,
} Phase;

typedef struct Program
{
    Peer* peer;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    // The phase's request, and in one phase a second made with it.
    mwinRequestId request;
    mwinRequestId second;
    int outcome;
    int secondOutcome;
    bool created;
    bool reading;
    xcb_atom_t png;
    xcb_atom_t big;
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

static bool FoundData(mwinContext* context, mwinRequestId read, const char* expected, size_t length)
{
    static char s_data[LIMIT];
    size_t found = 0;
    return mwinGetClipboardData(context, read, s_data, sizeof(s_data), &found) == mwin_success &&
           found == length && memcmp(s_data, expected, length) == 0;
}

static bool FoundText(mwinContext* context, mwinRequestId read, bool primary, const char* expected,
                      size_t length)
{
    char text[64];
    size_t found = 0;
    mwinResult status = primary ? mwinGetPrimaryText(context, read, text, sizeof(text), &found)
                                : mwinGetClipboardText(context, read, text, sizeof(text), &found);
    return status == mwin_success && found == length && memcmp(text, expected, length) == 0;
}

static void ReadData(Program* program, mwinContext* context, const char* mime,
                     mwinRequestId* requestOut)
{
    CHECK(mwinRequestClipboardReadData(context, program->window, mime, strlen(mime), requestOut) ==
              mwin_success,
          "a data read");
}

// The reads of the phases that wait for the other client to take a
// selection first.
static void StartReads(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseOtherBoth:
        CHECK(mwinRequestClipboardRead(context, program->window, &program->request) == mwin_success,
              "a text read");
        ReadData(program, context, PNG, &program->second);
        break;
    case phaseOtherPieces:
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

// Whether the phase is done: its requests answered, the other client's
// read finished, or, once the program has heard the other client took
// a selection, its reads answered.
static bool Ready(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
        return program->created;
    case phasePeerData:
    case phaseTargets:
    case phasePeerPieces:
    case phasePeerText:
    case phasePeerRefused:
    case phasePeerPrimary:
        return program->peer->gotAll;
    case phaseOtherBoth:
    case phaseOtherPieces:
    case phaseOtherPrimary:
    case phaseNoOwner:
        if (!program->reading && NowNs() - program->startNs >= SETTLE_NS)
        {
            StartReads(program, context);
            program->reading = true;
        }
        return program->reading && program->outcome >= 0 &&
               (program->phase != phaseOtherBoth || program->secondOutcome >= 0);
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
    Peer* peer = program->peer;
    switch (program->phase)
    {
    case phaseCreate:
        Write(program, context);
        break;
    case phaseWrite:
        CHECK(outcome == mwin_outcomeDone, "the program owns CLIPBOARD with data");
        PeerConvert(peer, peer->clipboard, program->png);
        break;
    case phasePeerData:
        CHECK(peer->gotLength == sizeof(s_png) && memcmp(peer->got, s_png, sizeof(s_png)) == 0 &&
                  peer->gotType == program->png,
              "another client reads an item as its type");
        PeerConvert(peer, peer->clipboard, peer->targets);
        break;
    case phaseTargets:
        CHECK(PeerGotTarget(peer, program->png) && PeerGotTarget(peer, program->big) &&
                  PeerGotTarget(peer, peer->utf8) && PeerGotTarget(peer, peer->textPlain),
              "its TARGETS name the types and the text");
        PeerConvert(peer, peer->clipboard, program->big);
        break;
    case phasePeerPieces:
        CHECK(peer->incremental && peer->gotLength == LARGE &&
                  memcmp(peer->got, program->large, LARGE) == 0 && peer->gotType == program->big,
              "a large item goes in pieces");
        PeerConvert(peer, peer->clipboard, peer->utf8);
        break;
    case phasePeerText:
        CHECK(peer->gotLength == 2 && memcmp(peer->got, "hi", 2) == 0,
              "the write's text is the clipboard's text");
        ReadData(program, context, PNG, &program->request);
        break;
    case phaseOwnData:
        CHECK(outcome == mwin_outcomeDone &&
                  FoundData(context, program->request, s_png, sizeof(s_png)),
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
        CHECK(outcome == mwin_outcomeDone, "the program owns CLIPBOARD with data alone");
        PeerConvert(peer, peer->clipboard, peer->utf8);
        break;
    case phasePeerRefused:
        CHECK(peer->refused, "data alone offers no text");
        CHECK(mwinRequestPrimaryWrite(context, program->window, "sel", 3, &program->request) ==
                  mwin_success,
              "a primary write");
        break;
    default:
        CHECK(outcome == mwin_outcomeDone, "the program owns PRIMARY");
        PeerConvert(peer, XCB_ATOM_PRIMARY, peer->utf8);
        break;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    Peer* peer = program->peer;
    int outcome = program->outcome;
    int second = program->secondOutcome;
    program->outcome = -1;
    program->secondOutcome = -1;
    program->reading = false;
    switch (program->phase)
    {
    case phasePeerPrimary:
        CHECK(peer->gotLength == 3 && memcmp(peer->got, "sel", 3) == 0,
              "another client reads PRIMARY");
        PeerOwn(peer, peer->clipboard, program->png, s_peerPng, sizeof(s_peerPng));
        break;
    case phaseOtherBoth:
        CHECK(outcome == mwin_outcomeDone && FoundText(context, program->request, false, "", 0),
              "a text read of data without text is empty");
        CHECK(second == mwin_outcomeDone &&
                  FoundData(context, program->second, s_peerPng, sizeof(s_peerPng)),
              "a data read made with it is answered after it");
        PeerOwn(peer, peer->clipboard, program->big, program->large, LARGE);
        break;
    case phaseOtherPieces:
        CHECK(outcome == mwin_outcomeDone &&
                  FoundData(context, program->request, program->large, LARGE),
              "another client's large item read in pieces");
        PeerOwn(peer, XCB_ATOM_PRIMARY, peer->utf8, "peer sel", 8);
        break;
    case phaseOtherPrimary:
        CHECK(outcome == mwin_outcomeDone &&
                  FoundText(context, program->request, true, "peer sel", 8),
              "another client's PRIMARY read");
        PeerOwn(peer, peer->clipboard, program->png, nullptr, 0);
        break;
    case phaseNoOwner:
        CHECK(outcome == mwin_outcomeFailed, "a data read with no owner fails");
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
    PeerPump(program->peer);
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
    const char* display = getenv("DISPLAY");
    static Peer peer;
    if (display == nullptr || display[0] == '\0' || !PeerStart(&peer))
    {
        return 77;
    }
    static char s_got[LIMIT + PEER_PIECE];
    static char s_large[LARGE];
    for (size_t i = 0; i < LARGE; i++)
    {
        s_large[i] = (char)(i % 251);
    }
    peer.got = s_got;
    unsetenv("WAYLAND_DISPLAY");
    Program program = {.peer = &peer,
                       .outcome = -1,
                       .secondOutcome = -1,
                       .png = Intern(peer.connection, PNG),
                       .big = Intern(peer.connection, BIG),
                       .large = s_large};
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.clipboardBytes = LIMIT;
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    xcb_disconnect(peer.connection);
    return s_failures == 0 ? 0 : 1;
}
