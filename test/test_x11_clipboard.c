// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's clipboard against another client, a second XCB
// connection of the test's: a write takes CLIPBOARD, and the other
// client reads it as UTF8_STRING and asks its TARGETS; a read while the
// program owns it answers from its own text; a large text goes to the
// other client in pieces (INCR) as text/plain;charset=utf-8, and one of
// the largest piece whole; once the
// other client owns CLIPBOARD, its text is read, repaired, whole and in
// pieces, too large past the limit, and empty with no owner. Skipped
// (exit status 77) without DISPLAY.

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
#define LARGE       (200u * 1024u)
// The largest text sent whole.
#define WHOLE (64u * 1024u)

typedef enum Phase
{
    phaseCreate,
    phaseWrite,
    phasePeerRead,
    phaseTargets,
    phaseOwnRead,
    phaseLargeWrite,
    phasePeerPieces,
    phaseWholeWrite,
    phasePeerWhole,
    phaseOtherText,
    phaseTooLarge,
    phasePieces,
    phaseNoOwner,
    phaseDone,
} Phase;

typedef struct Program
{
    Peer* peer;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
    int outcome;
    bool created;
    bool reading;
    // The large text's reads in pieces so far.
    int rounds;
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

static bool Found(mwinContext* context, mwinRequestId read, const char* expected, size_t length)
{
    static char s_text[LIMIT];
    size_t found = 0;
    return mwinGetClipboardText(context, read, s_text, sizeof(s_text), &found) == mwin_success &&
           found == length && memcmp(s_text, expected, length) == 0;
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
// read finished, or, once the program has heard the other client took
// CLIPBOARD, its read answered.
static bool Ready(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
        return program->created;
    case phasePeerRead:
    case phaseTargets:
    case phasePeerPieces:
    case phasePeerWhole:
        return program->peer->gotAll;
    case phaseOtherText:
    case phaseTooLarge:
    case phasePieces:
    case phaseNoOwner:
        if (!program->reading && NowNs() - program->startNs >= SETTLE_NS)
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
    Peer* peer = program->peer;
    switch (program->phase)
    {
    case phaseCreate:
        Write(program, context, "h\xC3\xA9llo", 6);
        break;
    case phaseWrite:
        CHECK(outcome == mwin_outcomeDone, "the program owns CLIPBOARD");
        PeerConvert(peer, peer->clipboard, peer->utf8);
        break;
    case phasePeerRead:
        CHECK(peer->gotLength == 6 && memcmp(peer->got, "h\xC3\xA9llo", 6) == 0 &&
                  peer->gotType == peer->utf8,
              "another client reads the text as UTF8_STRING");
        PeerConvert(peer, peer->clipboard, peer->targets);
        break;
    case phaseTargets:
        CHECK(PeerGotTarget(peer, peer->utf8) && PeerGotTarget(peer, peer->textPlain),
              "its TARGETS name both UTF-8 targets");
        Read(program, context);
        break;
    case phaseOwnRead:
        CHECK(outcome == mwin_outcomeDone && Found(context, program->request, "h\xC3\xA9llo", 6),
              "a read of the program's own selection");
        Write(program, context, program->large, LARGE);
        break;
    default:
        CHECK(outcome == mwin_outcomeDone, "a large write");
        PeerConvert(peer, peer->clipboard, peer->textPlain);
        break;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    static char s_tooLarge[LIMIT + 1];
    Peer* peer = program->peer;
    int outcome = program->outcome;
    program->outcome = -1;
    program->reading = false;
    switch (program->phase)
    {
    case phasePeerPieces:
        CHECK(peer->incremental && peer->gotLength == LARGE &&
                  memcmp(peer->got, program->large, LARGE) == 0 && peer->gotType == peer->textPlain,
              "a large text goes in pieces as text/plain;charset=utf-8");
        // More reads than readers served at once: each finished one
        // makes room.
        if (++program->rounds < 5)
        {
            PeerConvert(peer, peer->clipboard, peer->textPlain);
            program->startNs = NowNs();
            return;
        }
        Write(program, context, program->large, WHOLE);
        break;
    case phaseWholeWrite:
        CHECK(outcome == mwin_outcomeDone, "a write of the largest piece");
        PeerConvert(peer, peer->clipboard, peer->utf8);
        break;
    case phasePeerWhole:
        CHECK(!peer->incremental && peer->gotLength == WHOLE &&
                  memcmp(peer->got, program->large, WHOLE) == 0,
              "a text of the largest piece goes whole");
        PeerOwn(peer, peer->clipboard, peer->utf8, "A\xC3(", 3);
        break;
    case phaseOtherText:
        CHECK(outcome == mwin_outcomeDone && Found(context, program->request, "A\xEF\xBF\xBD(", 5),
              "another client's text read, repaired");
        memset(s_tooLarge, 'x', sizeof(s_tooLarge));
        PeerOwn(peer, peer->clipboard, peer->utf8, s_tooLarge, sizeof(s_tooLarge));
        break;
    case phaseTooLarge:
        CHECK(outcome == mwin_outcomeTooLarge, "text past the limit too large");
        PeerOwn(peer, peer->clipboard, peer->utf8, program->large, LARGE);
        break;
    case phasePieces:
        CHECK(outcome == mwin_outcomeDone &&
                  Found(context, program->request, program->large, LARGE),
              "another client's large text read in pieces");
        PeerOwn(peer, peer->clipboard, peer->utf8, nullptr, 0);
        break;
    case phaseNoOwner:
        CHECK(outcome == mwin_outcomeDone && Found(context, program->request, "", 0),
              "no owner read as empty");
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
        s_large[i] = (char)('a' + i % 26);
    }
    peer.got = s_got;
    unsetenv("WAYLAND_DISPLAY");
    Program program = {.peer = &peer, .outcome = -1, .large = s_large};
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
