// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's clipboard against another client, a second XCB
// connection of the test's: a write takes CLIPBOARD, and the other
// client reads it as UTF8_STRING and asks its TARGETS; a read while the
// program owns it answers from its own text; a large text goes to the
// other client in pieces (INCR) as text/plain;charset=utf-8; once the
// other client owns CLIPBOARD, its text is read, repaired, whole and in
// pieces, too large past the limit, and empty with no owner. Skipped
// (exit status 77) without DISPLAY.

#include "test_harness.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   50000000ull
#define LIMIT       (256u * 1024u)
#define LARGE       (200u * 1024u)
#define PIECE       (64u * 1024u)

// The other client: its window and atoms; the text it owns CLIPBOARD
// with and a transfer of it in pieces; and what its last read got.
typedef struct Peer
{
    xcb_connection_t* connection;
    xcb_window_t window;
    xcb_atom_t clipboard;
    xcb_atom_t utf8;
    xcb_atom_t textPlain;
    xcb_atom_t targets;
    xcb_atom_t incr;
    xcb_atom_t property;
    const char* text;
    size_t length;
    xcb_window_t sendTo;
    xcb_atom_t sendProperty;
    xcb_atom_t sendType;
    size_t sendOffset;
    bool incremental;
    bool gotAll;
    bool refused;
    xcb_atom_t gotType;
    char* got;
    size_t gotLength;
} Peer;

typedef enum Phase
{
    phaseCreate,
    phaseWrite,
    phasePeerRead,
    phaseTargets,
    phaseOwnRead,
    phaseLargeWrite,
    phasePeerPieces,
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

static xcb_atom_t Intern(xcb_connection_t* connection, const char* name)
{
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name), nullptr);
    xcb_atom_t atom = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

static bool PeerStart(Peer* peer)
{
    peer->connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(peer->connection) != 0)
    {
        return false;
    }
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(peer->connection)).data;
    peer->window = xcb_generate_id(peer->connection);
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(peer->connection, XCB_COPY_FROM_PARENT, peer->window, screen->root, 0, 0, 1,
                      1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK,
                      &mask);
    peer->clipboard = Intern(peer->connection, "CLIPBOARD");
    peer->utf8 = Intern(peer->connection, "UTF8_STRING");
    peer->textPlain = Intern(peer->connection, "text/plain;charset=utf-8");
    peer->targets = Intern(peer->connection, "TARGETS");
    peer->incr = Intern(peer->connection, "INCR");
    peer->property = Intern(peer->connection, "PEER_SELECTION");
    return true;
}

// Asks the owner of CLIPBOARD for a target.
static void PeerConvert(Peer* peer, xcb_atom_t target)
{
    peer->gotLength = 0;
    peer->gotAll = false;
    peer->refused = false;
    peer->incremental = false;
    xcb_convert_selection(peer->connection, peer->window, peer->clipboard, target, peer->property,
                          XCB_CURRENT_TIME);
    xcb_flush(peer->connection);
}

// Takes CLIPBOARD with text, or gives it to no one.
static void PeerOwn(Peer* peer, const char* text, size_t length)
{
    peer->text = text;
    peer->length = length;
    peer->sendTo = 0;
    xcb_set_selection_owner(peer->connection, text != nullptr ? peer->window : XCB_WINDOW_NONE,
                            peer->clipboard, XCB_CURRENT_TIME);
    xcb_flush(peer->connection);
}

// Takes the property of the peer's read: false when it was empty.
static bool PeerTake(Peer* peer)
{
    xcb_get_property_reply_t* reply =
        xcb_get_property_reply(peer->connection,
                               xcb_get_property(peer->connection, 1, peer->window, peer->property,
                                                XCB_GET_PROPERTY_TYPE_ANY, 0, UINT32_MAX / 4),
                               nullptr);
    int length = reply != nullptr ? xcb_get_property_value_length(reply) : 0;
    if (reply != nullptr && reply->type == peer->incr)
    {
        peer->incremental = true;
        length = 1;
    }
    else if (length > 0)
    {
        peer->gotType = reply->type;
        memcpy(peer->got + peer->gotLength, xcb_get_property_value(reply), (size_t)length);
        peer->gotLength += (size_t)length;
    }
    free(reply);
    return length > 0;
}

static void PeerNotify(Peer* peer, const xcb_selection_request_event_t* request,
                       xcb_atom_t property)
{
    union
    {
        xcb_selection_notify_event_t notify;
        char bytes[32];
    } event = {0};
    event.notify.response_type = XCB_SELECTION_NOTIFY;
    event.notify.time = request->time;
    event.notify.requestor = request->requestor;
    event.notify.selection = request->selection;
    event.notify.target = request->target;
    event.notify.property = property;
    xcb_send_event(peer->connection, 0, request->requestor, XCB_EVENT_MASK_NO_EVENT, event.bytes);
}

// Serves the peer's text as UTF8_STRING, whole or in pieces.
static void PeerServe(Peer* peer, const xcb_selection_request_event_t* request)
{
    if (request->target != peer->utf8)
    {
        PeerNotify(peer, request, XCB_ATOM_NONE);
        return;
    }
    if (peer->length <= PIECE)
    {
        xcb_change_property(peer->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            request->property, peer->utf8, 8, (uint32_t)peer->length, peer->text);
    }
    else
    {
        uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
        uint32_t length = (uint32_t)peer->length;
        xcb_change_window_attributes(peer->connection, request->requestor, XCB_CW_EVENT_MASK,
                                     &mask);
        xcb_change_property(peer->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            request->property, peer->incr, 32, 1, &length);
        peer->sendTo = request->requestor;
        peer->sendProperty = request->property;
        peer->sendType = peer->utf8;
        peer->sendOffset = 0;
    }
    PeerNotify(peer, request, request->property);
}

static void PeerProperty(Peer* peer, const xcb_property_notify_event_t* event)
{
    if (event->window == peer->window && event->atom == peer->property && peer->incremental &&
        event->state == XCB_PROPERTY_NEW_VALUE)
    {
        peer->gotAll = !PeerTake(peer);
    }
    else if (event->window == peer->sendTo && event->atom == peer->sendProperty &&
             event->state == XCB_PROPERTY_DELETE)
    {
        size_t left = peer->length - peer->sendOffset;
        uint32_t length = (uint32_t)(left < PIECE ? left : PIECE);
        xcb_change_property(peer->connection, XCB_PROP_MODE_REPLACE, peer->sendTo,
                            peer->sendProperty, peer->sendType, 8, length,
                            peer->text + peer->sendOffset);
        peer->sendOffset += length;
        peer->sendTo = length == 0 ? 0 : peer->sendTo;
    }
}

static void PeerPump(Peer* peer)
{
    xcb_generic_event_t* event = nullptr;
    while ((event = xcb_poll_for_event(peer->connection)) != nullptr)
    {
        uint8_t type = event->response_type & 0x7F;
        if (type == XCB_SELECTION_NOTIFY)
        {
            const xcb_selection_notify_event_t* notify = (const xcb_selection_notify_event_t*)event;
            peer->refused = notify->property == XCB_ATOM_NONE;
            peer->gotAll = peer->refused || (!PeerTake(peer) || !peer->incremental);
        }
        else if (type == XCB_SELECTION_REQUEST)
        {
            PeerServe(peer, (const xcb_selection_request_event_t*)event);
        }
        else if (type == XCB_PROPERTY_NOTIFY)
        {
            PeerProperty(peer, (const xcb_property_notify_event_t*)event);
        }
        free(event);
    }
    xcb_flush(peer->connection);
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

static bool Found(mwinContext* context, const char* expected, size_t length)
{
    static char s_text[LIMIT];
    size_t found = 0;
    return mwinGetClipboardText(context, s_text, sizeof(s_text), &found) == mwin_success &&
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

// Whether the other client's TARGETS has both UTF-8 targets.
static bool HasTargets(const Peer* peer)
{
    bool utf8 = false;
    bool textPlain = false;
    for (size_t i = 0; i + sizeof(xcb_atom_t) <= peer->gotLength; i += sizeof(xcb_atom_t))
    {
        xcb_atom_t atom = 0;
        memcpy(&atom, peer->got + i, sizeof(atom));
        utf8 = utf8 || atom == peer->utf8;
        textPlain = textPlain || atom == peer->textPlain;
    }
    return utf8 && textPlain;
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
        PeerConvert(peer, peer->utf8);
        break;
    case phasePeerRead:
        CHECK(peer->gotLength == 6 && memcmp(peer->got, "h\xC3\xA9llo", 6) == 0 &&
                  peer->gotType == peer->utf8,
              "another client reads the text as UTF8_STRING");
        PeerConvert(peer, peer->targets);
        break;
    case phaseTargets:
        CHECK(HasTargets(peer), "its TARGETS name both UTF-8 targets");
        Read(program, context);
        break;
    case phaseOwnRead:
        CHECK(outcome == mwin_outcomeDone && Found(context, "h\xC3\xA9llo", 6),
              "a read of the program's own selection");
        Write(program, context, program->large, LARGE);
        break;
    default:
        CHECK(outcome == mwin_outcomeDone, "a large write");
        PeerConvert(peer, peer->textPlain);
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
            PeerConvert(peer, peer->textPlain);
            program->startNs = NowNs();
            return;
        }
        PeerOwn(peer, "A\xC3(", 3);
        break;
    case phaseOtherText:
        CHECK(outcome == mwin_outcomeDone && Found(context, "A\xEF\xBF\xBD(", 5),
              "another client's text read, repaired");
        memset(s_tooLarge, 'x', sizeof(s_tooLarge));
        PeerOwn(peer, s_tooLarge, sizeof(s_tooLarge));
        break;
    case phaseTooLarge:
        CHECK(outcome == mwin_outcomeTooLarge, "text past the limit too large");
        PeerOwn(peer, program->large, LARGE);
        break;
    case phasePieces:
        CHECK(outcome == mwin_outcomeDone && Found(context, program->large, LARGE),
              "another client's large text read in pieces");
        PeerOwn(peer, nullptr, 0);
        break;
    case phaseNoOwner:
        CHECK(outcome == mwin_outcomeDone && Found(context, "", 0), "no owner read as empty");
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
    static char s_got[LIMIT + PIECE];
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
