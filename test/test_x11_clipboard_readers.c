// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's readers taking its large texts in pieces, five other
// clients at once: while three take CLIPBOARD's and one PRIMARY's, the
// fifth is refused, every reader's place taken; a new CLIPBOARD text
// drops its readers and leaves PRIMARY's, which takes its text whole.
// Skipped (exit status 77) without DISPLAY.

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
#define LARGE       (200u * 1024u)
// The peers: three of CLIPBOARD, one of PRIMARY, one too many.
#define PEERS   5
#define PRIMARY 3
#define REFUSED 4

typedef enum Phase
{
    phaseCreate,
    phaseClipboard,
    phasePrimary,
    phaseServed,
    phaseRewrite,
    phaseRead,
    phaseDone,
} Phase;

typedef struct Program
{
    Peer* peers;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
    int outcome;
    bool created;
    char* clipboard;
    char* primary;
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

// Whether the phase is done: its window made or its request answered,
// the program given time to serve the readers, or the PRIMARY reader
// done and the one too many refused. The readers take no piece before
// the new CLIPBOARD text.
static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return program->created;
    case phaseServed:
        return NowNs() - program->startNs >= SETTLE_NS;
    case phaseRead:
        for (int i = 0; i < PEERS; i++)
        {
            PeerPump(&program->peers[i]);
        }
        return program->peers[PRIMARY].gotAll && program->peers[REFUSED].gotAll;
    default:
        return program->outcome >= 0;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    Peer* peers = program->peers;
    int outcome = program->outcome;
    program->outcome = -1;
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(mwinRequestClipboardWrite(context, program->window, program->clipboard, LARGE,
                                        &program->request) == mwin_success,
              "a large CLIPBOARD text");
        break;
    case phaseClipboard:
        CHECK(outcome == mwin_outcomeDone &&
                  mwinRequestPrimaryWrite(context, program->window, program->primary, LARGE,
                                          &program->request) == mwin_success,
              "and a large PRIMARY text");
        break;
    case phasePrimary:
        CHECK(outcome == mwin_outcomeDone, "both owned");
        // In turn: the X server has sent one's request before the next
        // asks.
        for (int i = 0; i < PEERS; i++)
        {
            PeerConvert(&peers[i], i == PRIMARY ? XCB_ATOM_PRIMARY : peers[i].clipboard,
                        peers[i].utf8);
            free(xcb_get_input_focus_reply(peers[i].connection,
                                           xcb_get_input_focus(peers[i].connection), nullptr));
        }
        break;
    case phaseServed:
        CHECK(mwinRequestClipboardWrite(context, program->window, "new", 3, &program->request) ==
                  mwin_success,
              "a new CLIPBOARD text");
        break;
    case phaseRewrite:
        CHECK(outcome == mwin_outcomeDone, "the new text owned");
        break;
    default:
        CHECK(peers[REFUSED].refused, "the reader past the readers' places refused");
        CHECK(peers[PRIMARY].incremental && peers[PRIMARY].gotLength == LARGE &&
                  memcmp(peers[PRIMARY].got, program->primary, LARGE) == 0,
              "PRIMARY's reader takes its text whole in pieces");
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
    const char* display = getenv("DISPLAY");
    static Peer s_peers[PEERS];
    static char s_got[PEERS][LARGE + PEER_PIECE];
    for (int i = 0; i < PEERS; i++)
    {
        if (display == nullptr || display[0] == '\0' || !PeerStart(&s_peers[i]))
        {
            return 77;
        }
        s_peers[i].got = s_got[i];
    }
    static char s_clipboard[LARGE];
    static char s_primary[LARGE];
    for (size_t i = 0; i < LARGE; i++)
    {
        s_clipboard[i] = (char)('a' + i % 26);
        s_primary[i] = (char)('A' + i % 26);
    }
    unsetenv("WAYLAND_DISPLAY");
    Program program = {
        .peers = s_peers, .outcome = -1, .clipboard = s_clipboard, .primary = s_primary};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    for (int i = 0; i < PEERS; i++)
    {
        xcb_disconnect(s_peers[i].connection);
    }
    return s_failures == 0 ? 0 : 1;
}
