// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend's drag and drop against a source of the test's own,
// a second XCB connection speaking XDND 5: the window says it is
// XdndAware; a drag of files and text is accepted with the copy action
// and reported from its first position, a position that has not moved
// not reported again; the drop converts to text/uri-list, then to
// UTF8_STRING, delivers the paths (a URI of another scheme left out)
// and the text repaired, and finishes; types past three come from the
// source's XdndTypeList; a drag of neither is refused and not
// reported. Under a drop limit that is no whole number of 32-bit words,
// a text of exactly the limit is delivered whole. Skipped (exit status
// 77) without DISPLAY.

#include "test_harness.h"

#include "maul-window/drop.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   50000000ull
#define MAX_RECORDS 32

enum
{
    atomAware,
    atomEnter,
    atomPosition,
    atomStatus,
    atomLeave,
    atomDrop,
    atomFinished,
    atomSelection,
    atomTypeList,
    atomCopy,
    atomUriList,
    atomUtf8,
    atomTextPlain,
    atomHtml,
    atomPng,
    ATOMS,
};

static const char* const s_names[ATOMS] = {
    "XdndAware",
    "XdndEnter",
    "XdndPosition",
    "XdndStatus",
    "XdndLeave",
    "XdndDrop",
    "XdndFinished",
    "XdndSelection",
    "XdndTypeList",
    "XdndActionCopy",
    "text/uri-list",
    "UTF8_STRING",
    "text/plain;charset=utf-8",
    "text/html",
    "image/png",
};

// The source: its window, the program's window it drags over, and what
// the program answered.
typedef struct Source
{
    xcb_connection_t* connection;
    xcb_window_t root;
    xcb_window_t window;
    xcb_window_t target;
    xcb_atom_t atoms[ATOMS];
    int statuses;
    uint32_t status[5];
    bool finished;
    uint32_t finish[5];
    // The targets the program converted to, in order.
    xcb_atom_t asked[4];
    int askedCount;
    // The text served as UTF8_STRING.
    const char* text;
} Source;

typedef enum Phase
{
    phaseCreate,
    phaseEnter,
    phaseDrop,
    phaseTypeList,
    phaseTypeLeft,
    phaseNeither,
    phaseDone,
} Phase;

typedef struct Program
{
    Source* source;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinEvent records[MAX_RECORDS];
    int count;
    // The run under the drop limit: a drag of text dropped at once.
    bool limited;
    bool timedOut;
} Program;

// The drop limit of the limited run, and a text of exactly it.
#define LIMIT 10u
static const char s_limited[] = "0123456789";

static const char s_files[] = "file:///tmp/a.txt\r\nfile:///tmp/%C3%A9.png\r\nhttp://x/y\r\n";

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static bool SourceStart(Source* source)
{
    source->connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(source->connection) != 0)
    {
        return false;
    }
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(source->connection)).data;
    source->root = screen->root;
    source->window = xcb_generate_id(source->connection);
    xcb_create_window(source->connection, XCB_COPY_FROM_PARENT, source->window, screen->root, 0, 0,
                      1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
    xcb_intern_atom_cookie_t cookies[ATOMS];
    for (int i = 0; i < ATOMS; i++)
    {
        cookies[i] =
            xcb_intern_atom(source->connection, 0, (uint16_t)strlen(s_names[i]), s_names[i]);
    }
    for (int i = 0; i < ATOMS; i++)
    {
        xcb_intern_atom_reply_t* reply =
            xcb_intern_atom_reply(source->connection, cookies[i], nullptr);
        source->atoms[i] = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
        free(reply);
    }
    xcb_set_selection_owner(source->connection, source->window, source->atoms[atomSelection],
                            XCB_CURRENT_TIME);
    xcb_flush(source->connection);
    return true;
}

static void Send(Source* source, int type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    xcb_client_message_event_t message = {0};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = source->target;
    message.type = source->atoms[type];
    uint32_t data[5] = {source->window, d1, d2, d3, d4};
    memcpy(message.data.data32, data, sizeof(data));
    xcb_send_event(source->connection, 0, source->target, XCB_EVENT_MASK_NO_EVENT,
                   (const char*)&message);
    xcb_flush(source->connection);
}

// Enters with up to three types in the message, more in XdndTypeList.
static void Enter(Source* source, const int* types, int count)
{
    xcb_atom_t atoms[8] = {0};
    for (int i = 0; i < count; i++)
    {
        atoms[i] = source->atoms[types[i]];
    }
    if (count > 3)
    {
        xcb_change_property(source->connection, XCB_PROP_MODE_REPLACE, source->window,
                            source->atoms[atomTypeList], XCB_ATOM_ATOM, 32, (uint32_t)count, atoms);
    }
    Send(source, atomEnter, (5u << 24) | (count > 3 ? 1u : 0u), atoms[0], atoms[1], atoms[2]);
}

// A position of the program's window, sent in root coordinates.
static void Position(Source* source, int16_t x, int16_t y)
{
    xcb_translate_coordinates_reply_t* reply = xcb_translate_coordinates_reply(
        source->connection,
        xcb_translate_coordinates(source->connection, source->target, source->root, x, y), nullptr);
    uint32_t packed =
        reply != nullptr ? ((uint32_t)(uint16_t)reply->dst_x << 16) | (uint16_t)reply->dst_y : 0;
    free(reply);
    Send(source, atomPosition, 0, packed, XCB_CURRENT_TIME, source->atoms[atomCopy]);
}

static void Serve(Source* source, const xcb_selection_request_event_t* request)
{
    const xcb_atom_t* atoms = source->atoms;
    const char* text = request->target == atoms[atomUriList] ? s_files
                       : request->target == atoms[atomUtf8]  ? source->text
                                                             : nullptr;
    if (source->askedCount < 4)
    {
        source->asked[source->askedCount++] = request->target;
    }
    if (text != nullptr)
    {
        xcb_change_property(source->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            request->property, request->target, 8, (uint32_t)strlen(text), text);
    }
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
    event.notify.property = text != nullptr ? request->property : XCB_ATOM_NONE;
    xcb_send_event(source->connection, 0, request->requestor, XCB_EVENT_MASK_NO_EVENT, event.bytes);
}

static void SourcePump(Source* source)
{
    xcb_generic_event_t* event = nullptr;
    while ((event = xcb_poll_for_event(source->connection)) != nullptr)
    {
        uint8_t type = event->response_type & 0x7F;
        const xcb_client_message_event_t* message = (const xcb_client_message_event_t*)event;
        if (type == XCB_CLIENT_MESSAGE && message->type == source->atoms[atomStatus])
        {
            source->statuses += 1;
            memcpy(source->status, message->data.data32, sizeof(source->status));
        }
        else if (type == XCB_CLIENT_MESSAGE && message->type == source->atoms[atomFinished])
        {
            source->finished = true;
            memcpy(source->finish, message->data.data32, sizeof(source->finish));
        }
        else if (type == XCB_SELECTION_REQUEST)
        {
            Serve(source, (const xcb_selection_request_event_t*)event);
        }
        free(event);
    }
    xcb_flush(source->connection);
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

// The drag records among the drained ones: other records (focus moving
// between the tests' windows) may come too.
static int DragRecords(const Program* program)
{
    int count = 0;
    for (int i = 0; i < program->count; i++)
    {
        mwinEventType type = program->records[i].type;
        count += type >= mwin_eventDragEntered && type <= mwin_eventDropped;
    }
    return count;
}

static bool DragAt(const mwinEvent* event, float x, float y, mwinDragContents contents)
{
    return event != nullptr && event->data.drag.position.x == x &&
           event->data.drag.position.y == y && event->data.drag.contents == contents;
}

static bool Aware(const Source* source)
{
    xcb_get_property_reply_t* reply =
        xcb_get_property_reply(source->connection,
                               xcb_get_property(source->connection, 0, source->target,
                                                source->atoms[atomAware], XCB_ATOM_ATOM, 0, 1),
                               nullptr);
    bool aware = reply != nullptr && xcb_get_property_value_length(reply) == 4 &&
                 *(const uint32_t*)xcb_get_property_value(reply) == 5;
    free(reply);
    return aware;
}

static void CheckDrop(Program* program, mwinContext* context)
{
    Source* source = program->source;
    const mwinEvent* dropped = Find(program, mwin_eventDropped, 0);
    CHECK(DragAt(Find(program, mwin_eventDragMoved, 0), 30.0f, 40.0f,
                 mwin_dragFiles | mwin_dragText) &&
              Find(program, mwin_eventDragMoved, 1) == nullptr && dropped != nullptr,
          "moved once, a still position not reported again, then dropped");
    CHECK(source->askedCount == 2 && source->asked[0] == source->atoms[atomUriList] &&
              source->asked[1] == source->atoms[atomUtf8] && (source->finish[1] & 1u) != 0 &&
              source->finish[2] == source->atoms[atomCopy],
          "converted to the files, then the text, and finished with a copy");
    const mwinDropEvent* drop = &dropped->data.drop;
    char bytes[64];
    size_t length = 0;
    static const char paths[] = "/tmp/a.txt\0/tmp/\xC3\xA9.png";
    CHECK(drop->fileCount == 2 && drop->truncated &&
              mwinGetDroppedFiles(context, drop->drop, bytes, sizeof(bytes), &length) ==
                  mwin_success &&
              length == sizeof(paths) && memcmp(bytes, paths, length) == 0 &&
              mwinGetDroppedText(context, drop->drop, bytes, sizeof(bytes), &length) ==
                  mwin_success &&
              length == 6 && memcmp(bytes, "hi\xEF\xBF\xBD(", 6) == 0,
          "the paths, another scheme left out, and the text repaired");
    // The text types only past the three the message holds.
    static const int types[] = {atomHtml, atomPng, atomHtml, atomTextPlain, atomUtf8};
    Enter(source, types, 5);
    Position(source, 5, 5);
}

static void CheckLimited(const Program* program, mwinContext* context)
{
    const mwinEvent* dropped = Find(program, mwin_eventDropped, 0);
    char bytes[LIMIT];
    size_t length = 0;
    CHECK(dropped != nullptr &&
              mwinGetDroppedText(context, dropped->data.drop.drop, bytes, sizeof(bytes), &length) ==
                  mwin_success &&
              length == LIMIT && memcmp(bytes, s_limited, LIMIT) == 0,
          "a text of exactly the limit delivered whole");
}

static bool Ready(Program* program)
{
    const Source* source = program->source;
    switch (program->phase)
    {
    case phaseCreate:
        // The backend's requests reach the server at its next flush,
        // before any source could ask.
        return source->target != 0 && Aware(source);
    case phaseEnter:
        return source->statuses >= 1 && Find(program, mwin_eventDragEntered, 0) != nullptr;
    case phaseDrop:
        return source->finished;
    case phaseTypeList:
        return Find(program, mwin_eventDragEntered, 0) != nullptr;
    case phaseTypeLeft:
        return Find(program, mwin_eventDragLeft, 0) != nullptr;
    default:
        return source->statuses >= 1 && NowNs() - program->startNs >= SETTLE_NS;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    Source* source = program->source;
    switch (program->phase)
    {
    case phaseCreate:
    {
        static const int types[] = {atomUriList, atomUtf8, atomHtml};
        Enter(source, program->limited ? types + 1 : types, program->limited ? 1 : 3);
        Position(source, 10, 20);
        if (program->limited)
        {
            Send(source, atomDrop, 0, XCB_CURRENT_TIME, 0, 0);
            program->phase = phaseEnter;
        }
        break;
    }
    case phaseEnter:
        CHECK((source->status[1] & 1u) != 0 && source->status[4] == source->atoms[atomCopy] &&
                  DragAt(Find(program, mwin_eventDragEntered, 0), 10.0f, 20.0f,
                         mwin_dragFiles | mwin_dragText),
              "a drag of files and text accepted with a copy, entered at its first position");
        Position(source, 30, 40);
        Position(source, 30, 40);
        Send(source, atomDrop, 0, XCB_CURRENT_TIME, 0, 0);
        break;
    case phaseDrop:
        if (program->limited)
        {
            CheckLimited(program, context);
            program->phase = phaseNeither;
            break;
        }
        CheckDrop(program, context);
        break;
    case phaseTypeList:
        CHECK(DragAt(Find(program, mwin_eventDragEntered, 0), 5.0f, 5.0f, mwin_dragText),
              "types past three from XdndTypeList");
        Send(source, atomLeave, 0, 0, 0, 0);
        break;
    case phaseTypeLeft:
    {
        static const int types[] = {atomHtml};
        source->statuses = 0;
        Enter(source, types, 1);
        Position(source, 5, 5);
        break;
    }
    default:
        CHECK((source->status[1] & 1u) == 0 && DragRecords(program) == 0,
              "a drag of neither refused and not reported");
        Send(source, atomLeave, 0, 0, 0, 0);
        break;
    }
    program->phase += 1;
    program->count = 0;
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
    SourcePump(program->source);
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
    mwinNativeHandles handles;
    if (program->source->target == 0 && Find(program, mwin_eventWindowCreated, 0) != nullptr)
    {
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success,
              "the handles");
        program->source->target = handles.handles.x11.window;
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
    const char* display = getenv("DISPLAY");
    static Source source;
    if (display == nullptr || display[0] == '\0' || !SourceStart(&source))
    {
        return 77;
    }
    unsetenv("WAYLAND_DISPLAY");
    source.text = "hi\xC3(";
    Program program = {.source = &source};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    // A new window, and a source that has heard nothing of it.
    source.target = 0;
    source.finished = false;
    source.askedCount = 0;
    source.text = s_limited;
    program = (Program){.source = &source, .limited = true};
    def.context.limits.dropBytes = LIMIT;
    CHECK(mwinRun(&def) == mwin_success && !program.timedOut && program.phase == phaseDone,
          "the limited run");
    xcb_disconnect(source.connection);
    return s_failures == 0 ? 0 : 1;
}
