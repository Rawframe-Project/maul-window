// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the X11 backend's drag and drop (x11_drop.c) as other clients
// drive it: XdndEnter, XdndPosition, XdndLeave and XdndDrop client
// messages from any source to any window, the source's XdndTypeList, and
// the SelectionNotify answers to a drop's conversions with the property
// the stand-in X server then holds, whole, in pieces or of any type and
// format. Any client on the display can send all of them. Each input
// runs on a context of the test backend whose windows the X11 code knows
// by X ids of the stand-in's; the input is a script of messages, answers,
// deadlines and windows destroyed. Each window's drag records must come
// in order (entered, then moved, then left or dropped), every drop the
// backend took must finish once with an XdndFinished to its source, and
// what a drop delivers must be UTF-8 within the limit.

#include "x11_drop.h"

#include "maul-unicode/encoding.h"
#include "maul-window/drop.h"
#include "maul-window/window.h"

#include <stdlib.h>
#include <string.h>

#define WINDOWS 3
#define LIMIT   1024u
#define FILES   8u

// The X ids of the program's windows start here, one apart per window
// made; a client's windows and the drag sources are others.
#define FIRST_WINDOW 0x200001u
#define OTHER        0x400001u
#define SOURCE       0x600001u
// An atom no type of the backend's is.
#define UNKNOWN_ATOM 900u

static const uint8_t* s_data;
static size_t s_size;

// What the stand-in X server answers next: a property (none fails the
// reply) of a type and format, its bytes, and whether translating a
// position works.
static struct
{
    bool property;
    xcb_atom_t type;
    uint8_t format;
    uint32_t length;
    uint8_t bytes[2 * LIMIT + 8];
    bool translates;
} s_server;

static mwinX11Platform s_platform;
static mwinX11Window s_windows[WINDOWS];
static xcb_screen_t s_screen;
static mwinWindowId s_ids[WINDOWS];
static uint32_t s_nextWindow;

// The XdndFinished messages sent, and whether each window has a drag
// reported entered.
static uint32_t s_finished;
static bool s_entered[WINDOWS];

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static uint8_t Byte(void)
{
    if (s_size == 0)
    {
        return 0;
    }
    s_size--;
    return *s_data++;
}

static bool IsUtf8(const char* text, size_t length)
{
    return muniValidateUtf8(text, length).status == muni_success;
}

// The stand-in X server. The cookie of a property carries the words
// asked for to the reply; replies are the system's memory, which the
// backend gives back.
static xcb_get_property_cookie_t GetProperty(xcb_connection_t*, uint8_t, xcb_window_t, xcb_atom_t,
                                             xcb_atom_t, uint32_t, uint32_t length)
{
    return (xcb_get_property_cookie_t){length};
}

// The property as the X server sends it: at most the words asked for,
// in whole units of its format, the rest counted in bytes_after.
static xcb_get_property_reply_t*
GetPropertyReply(xcb_connection_t*, xcb_get_property_cookie_t cookie, xcb_generic_error_t**)
{
    if (!s_server.property)
    {
        return nullptr;
    }
    uint32_t unit = s_server.format / 8u;
    uint64_t asked = (uint64_t)cookie.sequence * 4u;
    uint32_t bytes = unit == 0 ? 0 : s_server.length - s_server.length % unit;
    uint32_t sent = bytes < asked ? bytes : (uint32_t)(asked - asked % (unit == 0 ? 1 : unit));
    xcb_get_property_reply_t* reply = calloc(1, sizeof(*reply) + sent + 1);
    Expect(reply != nullptr);
    reply->format = s_server.format;
    reply->type = s_server.type;
    reply->value_len = unit == 0 ? 0 : sent / unit;
    reply->bytes_after = bytes - sent;
    memcpy(reply + 1, s_server.bytes, sent);
    return reply;
}

static void* GetPropertyValue(const xcb_get_property_reply_t* reply)
{
    return (void*)(reply + 1);
}

static int GetPropertyValueLength(const xcb_get_property_reply_t* reply)
{
    return (int)(reply->value_len * (reply->format / 8u));
}

static xcb_translate_coordinates_cookie_t TranslateCoordinates(xcb_connection_t*, xcb_window_t,
                                                               xcb_window_t, int16_t x, int16_t y)
{
    return (xcb_translate_coordinates_cookie_t){((uint32_t)(uint16_t)x << 16) | (uint16_t)y};
}

static xcb_translate_coordinates_reply_t*
TranslateCoordinatesReply(xcb_connection_t*, xcb_translate_coordinates_cookie_t cookie,
                          xcb_generic_error_t**)
{
    if (!s_server.translates)
    {
        return nullptr;
    }
    xcb_translate_coordinates_reply_t* reply = calloc(1, sizeof(*reply));
    Expect(reply != nullptr);
    reply->dst_x = (int16_t)(cookie.sequence >> 16);
    reply->dst_y = (int16_t)(cookie.sequence & 0xFFFFu);
    return reply;
}

// A message to the drag's source: a status while it drags, a finish
// when it drops.
static xcb_void_cookie_t SendEvent(xcb_connection_t*, uint8_t, xcb_window_t destination, uint32_t,
                                   const char* event)
{
    const xcb_client_message_event_t* message = (const xcb_client_message_event_t*)event;
    Expect(message->response_type == XCB_CLIENT_MESSAGE && message->format == 32 &&
           message->window == destination);
    if (message->type == s_platform.atoms[mwin_atomXdndFinished])
    {
        s_finished++;
    }
    else
    {
        Expect(message->type == s_platform.atoms[mwin_atomXdndStatus]);
    }
    return (xcb_void_cookie_t){1};
}

static xcb_void_cookie_t ConvertSelection(xcb_connection_t*, xcb_window_t requestor, xcb_atom_t,
                                          xcb_atom_t, xcb_atom_t, xcb_timestamp_t)
{
    // The drop is converted into one of the program's windows.
    Expect(mwinX11SlotOf(&s_platform, requestor) >= 0);
    return (xcb_void_cookie_t){1};
}

// A type, from the input: one the backend takes, one it does not, or
// none.
static xcb_atom_t NextType(void)
{
    const xcb_atom_t* atoms = s_platform.atoms;
    xcb_atom_t types[] = {atoms[mwin_atomUriList],
                          atoms[mwin_atomTextPlainUtf8],
                          atoms[mwin_atomUtf8String],
                          XCB_ATOM_NONE,
                          UNKNOWN_ATOM,
                          atoms[mwin_atomIncr]};
    return types[Byte() % 6];
}

// The window a message names: one of the program's, a client's, or
// none.
static xcb_window_t NextWindow(uint8_t choice)
{
    choice %= WINDOWS + 2;
    if (choice < WINDOWS)
    {
        return s_windows[choice].window;
    }
    return choice == WINDOWS ? OTHER : 0;
}

// The property the stand-in holds next, from the input: none, or a type,
// a format and a length up to twice the limit, its bytes a beginning a
// file list or text would have, then the input's, then a pattern.
static void NextProperty(void)
{
    static const char* const starts[] = {"file:///tmp/", "file://localhost/home/a%20b\r\n",
                                         "http://x/\r\n", "", "caf\xc3\xa9 "};
    static const uint8_t formats[] = {0, 8, 16, 32};
    uint8_t shape = Byte();
    s_server.property = (shape & 7) != 0;
    s_server.type = NextType();
    s_server.format = formats[(shape >> 3) & 3];
    uint32_t length = Byte();
    length |= (uint32_t)Byte() << 8;
    s_server.length = length % (2 * LIMIT + 8);
    const char* start = starts[((shape >> 5) & 7) % 5];
    uint32_t at = 0;
    for (; start[at] != '\0' && at < s_server.length; at++)
    {
        s_server.bytes[at] = (uint8_t)start[at];
    }
    uint32_t own = Byte();
    for (uint32_t i = 0; i < own && at < s_server.length; i++)
    {
        s_server.bytes[at++] = Byte();
    }
    uint8_t pattern[4] = {Byte(), Byte(), Byte(), Byte()};
    for (uint32_t i = 0; at < s_server.length; i++)
    {
        s_server.bytes[at++] = pattern[i % 4];
    }
}

static void Message(uint8_t op)
{
    const xcb_atom_t* atoms = s_platform.atoms;
    xcb_atom_t kinds[] = {atoms[mwin_atomXdndEnter], atoms[mwin_atomXdndPosition],
                          atoms[mwin_atomXdndLeave], atoms[mwin_atomXdndDrop]};
    xcb_client_message_event_t message = {0};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.type = kinds[(op >> 3) & 3];
    message.window = NextWindow(Byte());
    uint32_t* data = message.data.data32;
    // One of two sources, or one the drag is not from.
    data[0] = SOURCE + (op >> 5) % 3;
    if (message.type == atoms[mwin_atomXdndEnter])
    {
        // The version, and whether more types than three wait in the
        // source's XdndTypeList, which the stand-in then holds.
        data[1] = ((uint32_t)Byte() << 24) | (Byte() & 1u);
        for (int i = 2; i < 5; i++)
        {
            data[i] = NextType();
        }
        if ((data[1] & 1u) != 0)
        {
            NextProperty();
        }
    }
    else if (message.type == atoms[mwin_atomXdndPosition])
    {
        data[2] =
            ((uint32_t)Byte() << 24) | ((uint32_t)Byte() << 16) | ((uint32_t)Byte() << 8) | Byte();
        data[4] = Byte() % 2 == 0 ? atoms[mwin_atomXdndActionCopy] : UNKNOWN_ATOM;
        s_server.translates = Byte() % 4 != 0;
    }
    else if (message.type == atoms[mwin_atomXdndDrop])
    {
        data[2] = Byte();
    }
    (void)mwinX11HandleDropEvent(&s_platform, (const xcb_generic_event_t*)&message);
}

// An answer to a conversion: into a window, of the drop's selection or
// another, with the property or none, which the stand-in then holds.
static void Answer(uint8_t op)
{
    xcb_selection_notify_event_t notify = {0};
    notify.response_type = XCB_SELECTION_NOTIFY;
    notify.requestor = NextWindow(Byte());
    notify.selection = (op & 0x08) != 0 ? s_platform.atoms[mwin_atomXdndSelection]
                                        : s_platform.atoms[mwin_atomClipboard];
    notify.property = (op & 0x10) != 0 ? XCB_ATOM_NONE : s_platform.atoms[mwin_atomSelection];
    NextProperty();
    (void)mwinX11HandleDropEvent(&s_platform, (const xcb_generic_event_t*)&notify);
}

// A drop's paths are its count of non-empty UTF-8 paths and its text is
// UTF-8, each within the limit.
static void CheckDrop(const mwinContext* context, const mwinDropEvent* drop)
{
    static char bytes[LIMIT + 1];
    size_t length = 0;
    Expect(mwinContext_GetDroppedFiles(context, drop->drop, bytes, sizeof(bytes), &length) ==
               mwin_success &&
           length <= LIMIT);
    uint32_t count = 0;
    for (size_t at = 0; at < length; count++)
    {
        const char* end = memchr(bytes + at, '\0', length - at);
        Expect(end != nullptr);
        size_t path = (size_t)(end - (bytes + at));
        Expect(path > 0 && IsUtf8(bytes + at, path));
        at += path + 1;
    }
    Expect(count == drop->fileCount && count <= FILES);
    Expect(mwinContext_GetDroppedText(context, drop->drop, bytes, sizeof(bytes), &length) ==
               mwin_success &&
           length == drop->textLength && length <= LIMIT && IsUtf8(bytes, length));
}

// Each window's drag records in order: entered, moved, then left or
// dropped.
static void Drain(mwinContext* context)
{
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        int i = 0;
        while (i < WINDOWS && memcmp(&s_ids[i], &event.window, sizeof(mwinWindowId)) != 0)
        {
            i++;
        }
        switch (event.type)
        {
        case mwin_eventDragEntered:
            Expect(i < WINDOWS && !s_entered[i]);
            s_entered[i] = true;
            break;
        case mwin_eventDragMoved:
            Expect(i < WINDOWS && s_entered[i]);
            break;
        case mwin_eventDragLeft:
            Expect(i < WINDOWS && s_entered[i]);
            s_entered[i] = false;
            break;
        case mwin_eventDropped:
            Expect(i < WINDOWS && s_entered[i]);
            s_entered[i] = false;
            CheckDrop(context, &event.data.drop);
            break;
        default:
            break;
        }
    }
}

// A window of the program's with an X id of the stand-in's, as the
// backend would make it.
static void Make(mwinContext* context, int i)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    Expect(mwinCreateWindow(context, &def, &s_ids[i], nullptr) == mwin_success);
    s_windows[i] = (mwinX11Window){.platform = &s_platform,
                                   .slot = s_ids[i].index1 - 1,
                                   .window = FIRST_WINDOW + s_nextWindow++};
    s_entered[i] = false;
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    // The X11 code finds a window by its slot; this one's slots are
    // the test backend's, three of them.
    static mwinX11Window bySlot[8];
    s_platform = (mwinX11Platform){.context = context, .screen = &s_screen, .scale = 1.0f};
    s_platform.connection = (xcb_connection_t*)&s_screen;
    s_platform.windows = bySlot;
    s_platform.drag.slot = -1;
    for (int i = 0; i < MWIN_X11_ATOMS; i++)
    {
        s_platform.atoms[i] = (xcb_atom_t)(100 + i);
    }
    mwinX11Api* api = &s_platform.api;
    api->getProperty = GetProperty;
    api->getPropertyReply = GetPropertyReply;
    api->getPropertyValue = GetPropertyValue;
    api->getPropertyValueLength = GetPropertyValueLength;
    api->translateCoordinates = TranslateCoordinates;
    api->translateCoordinatesReply = TranslateCoordinatesReply;
    api->sendEvent = SendEvent;
    api->convertSelection = ConvertSelection;
    s_nextWindow = 0;
    s_finished = 0;
    memset(bySlot, 0, sizeof(bySlot));

    for (int i = 0; i < WINDOWS; i++)
    {
        Make(context, i);
        bySlot[s_windows[i].slot] = s_windows[i];
    }
    while (s_size > 0)
    {
        uint8_t op = Byte();
        bool dropping = s_platform.drag.dropping;
        uint32_t finished = s_finished;
        switch (op & 7)
        {
        case 0:
        case 1:
        case 2:
        case 3:
            Message(op);
            break;
        case 4:
        case 5:
            Answer(op);
            break;
        case 6:
            // A drop past its deadline ends truncated.
            mwinX11CheckDrop(&s_platform, (op & 0x08) != 0 ? UINT64_MAX : 0);
            break;
        default:
        {
            // A window goes, perhaps under a drag or a drop, and another
            // takes its slot with a new X id.
            int i = (int)(((op >> 3) & 3) % WINDOWS);
            mwinX11ForgetDrag(&s_platform, s_windows[i].slot);
            bySlot[s_windows[i].slot].window = 0;
            Expect(mwinDestroyWindow(context, s_ids[i]) == mwin_success);
            Make(context, i);
            bySlot[s_windows[i].slot] = s_windows[i];
            break;
        }
        }
        // A drop the backend took ends with one XdndFinished.
        Expect(!dropping || s_platform.drag.dropping || s_finished == finished + 1);
        Drain(context);
    }
    // A drop under way ends at its deadline.
    mwinX11CheckDrop(&s_platform, UINT64_MAX);
    Drain(context);
    Expect(!s_platform.drag.dropping);
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_frameStop;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    s_data = data;
    s_size = size;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.context.limits.dropBytes = LIMIT;
    def.context.limits.droppedFiles = FILES;
    def.init = Init;
    def.frame = Frame;
    Expect(mwinRun(&def) == mwin_success);
    return 0;
}
