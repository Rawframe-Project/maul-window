// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the X11 backend's reads of the clipboard and the primary
// selection as other clients answer them: the owner's SelectionNotify,
// the property it leaves on the hidden window, whole or in INCR pieces,
// and the events any client can send or cause meanwhile. Each input runs
// on a context of the test backend whose clipboard and primary requests
// go through the X11 code (x11_clipboard_read.c, selection_reads.c)
// against a stand-in X server; the input is a script of requests, writes
// that win or lose the selection, events, deadlines and windows
// destroyed, each event with the property the stand-in then holds. Every
// request must complete exactly once, and what a read found must be
// within the clipboard limit, its text UTF-8.

#include "x11_clipboard.h"

#include "maul-unicode/encoding.h"
#include "maul-window/clipboard.h"
#include "maul-window/window.h"

#include <stdlib.h>
#include <string.h>

#define WINDOWS 3
#define LIMIT   1024u
// The requests kept: more than an input of 256 bytes can make.
#define TRACKED 128

// The hidden window the stand-in hands the backend, and a client's.
#define HIDDEN 0x200001u
#define OTHER  0x400001u

static const uint8_t* s_data;
static size_t s_size;

// What the stand-in X server answers next.
static struct
{
    // A property: its type, format and bytes, the bytes a pattern
    // repeated; or none, which fails the reply.
    bool property;
    xcb_atom_t type;
    uint8_t format;
    uint32_t length;
    uint8_t pattern[4];
    // Whether interning finds a type's atom, and whether setting the
    // selection's owner is kept.
    bool interned;
    bool owner;
} s_server;

static mwinX11Platform s_platform;
static xcb_screen_t s_screen;
static mwinBackendOps s_ops;
static const mwinBackendOps* s_test;

// Every request made, and the completions each got.
static struct
{
    mwinRequestId id;
    mwinRequestKind kind;
    uint32_t completions;
} s_requests[TRACKED];
static uint32_t s_made;

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

// The stand-in X server, which ignores what it is not asked about:
// replies are the system's memory, which the backend gives back.
static uint32_t GenerateId(xcb_connection_t*)
{
    return HIDDEN;
}

static xcb_generic_error_t* RequestCheck(xcb_connection_t*, xcb_void_cookie_t)
{
    return nullptr;
}

static xcb_void_cookie_t CreateWindowChecked(xcb_connection_t*, uint8_t, xcb_window_t, xcb_window_t,
                                             int16_t, int16_t, uint16_t, uint16_t, uint16_t,
                                             uint16_t, xcb_visualid_t, uint32_t, const void*)
{
    return (xcb_void_cookie_t){1};
}

static xcb_void_cookie_t DestroyWindow(xcb_connection_t*, xcb_window_t)
{
    return (xcb_void_cookie_t){1};
}

static xcb_intern_atom_cookie_t InternAtom(xcb_connection_t*, uint8_t, uint16_t, const char*)
{
    return (xcb_intern_atom_cookie_t){1};
}

static xcb_intern_atom_reply_t* InternAtomReply(xcb_connection_t*, xcb_intern_atom_cookie_t,
                                                xcb_generic_error_t**)
{
    xcb_intern_atom_reply_t* reply = calloc(1, sizeof(*reply));
    Expect(reply != nullptr);
    reply->atom = s_server.interned ? 900 : XCB_ATOM_NONE;
    return reply;
}

// The cookie carries the words asked for to the reply.
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
    uint8_t* value = (uint8_t*)(reply + 1);
    for (uint32_t i = 0; i < sent; i++)
    {
        value[i] = s_server.pattern[i % 4];
    }
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

static xcb_void_cookie_t SetSelectionOwner(xcb_connection_t*, xcb_window_t, xcb_atom_t,
                                           xcb_timestamp_t)
{
    return (xcb_void_cookie_t){1};
}

static xcb_get_selection_owner_cookie_t GetSelectionOwner(xcb_connection_t*, xcb_atom_t)
{
    return (xcb_get_selection_owner_cookie_t){1};
}

static xcb_get_selection_owner_reply_t*
GetSelectionOwnerReply(xcb_connection_t*, xcb_get_selection_owner_cookie_t, xcb_generic_error_t**)
{
    xcb_get_selection_owner_reply_t* reply = calloc(1, sizeof(*reply));
    Expect(reply != nullptr);
    reply->owner = s_server.owner ? HIDDEN : OTHER;
    return reply;
}

static xcb_void_cookie_t ConvertSelection(xcb_connection_t*, xcb_window_t, xcb_atom_t, xcb_atom_t,
                                          xcb_atom_t, xcb_timestamp_t)
{
    return (xcb_void_cookie_t){1};
}

// The clipboard's requests go to the X11 code, as mwinX11Submit sends
// them.
static void Submit(mwinContext* context, uint32_t slot, uint32_t index)
{
    context->backend = s_test;
    const mwinRequest* request = &context->windows[slot].requests[index];
    bool write = request->kind == mwin_requestClipboardWrite ||
                 request->kind == mwin_requestClipboardWriteData ||
                 request->kind == mwin_requestPrimaryWrite;
    int outcome = write ? mwinX11WriteSelection(&s_platform, request->kind)
                        : mwinX11ReadSelection(&s_platform, request);
    if (outcome >= 0)
    {
        mwinComplete(context, slot, index, (mwinOutcome)outcome);
    }
}

// The test backend's clock, which the core reads when it completes a
// request the new one supersedes, before handing the new one over. The
// context itself is not const.
static uint64_t Now(const mwinContext* context)
{
    mwinContext* own = (mwinContext*)context;
    own->backend = s_test;
    uint64_t now = s_test->now(own);
    own->backend = &s_ops;
    return now;
}

// Keeps a request made through the X11 code. The test backend knows
// itself by its operations, so they are swapped only until the core
// hands the request over, within the call; Submit swaps them back. On
// the way the core calls no operation but now and submit.
static void Track(mwinContext* context, mwinResult result, mwinRequestId id, mwinRequestKind kind)
{
    context->backend = s_test;
    if (result == mwin_success && s_made < TRACKED)
    {
        s_requests[s_made++] = (typeof(s_requests[0])){id, kind, 0};
    }
}

// What a done read left must be within the limit, its text UTF-8; a
// later read may have replaced it.
static void CheckFound(const mwinContext* context, const mwinCompletion* completion)
{
    static char found[LIMIT];
    size_t length = 0;
    mwinResult result = mwin_errorStale;
    switch (completion->kind)
    {
    case mwin_requestClipboardRead:
        result = mwinRequest_GetClipboardText(context, completion->request, found, LIMIT, &length);
        break;
    case mwin_requestPrimaryRead:
        result = mwinRequest_GetPrimaryText(context, completion->request, found, LIMIT, &length);
        break;
    case mwin_requestClipboardReadData:
        result = mwinRequest_GetClipboardData(context, completion->request, found, LIMIT, &length);
        break;
    default:
        return;
    }
    Expect(result == mwin_success || result == mwin_errorStale);
    Expect(result != mwin_success ||
           (length <= LIMIT && (completion->kind == mwin_requestClipboardReadData ||
                                muniValidateUtf8(found, length).status == muni_success)));
}

static void Drain(mwinContext* context)
{
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        if (event.type != mwin_eventRequestCompleted)
        {
            continue;
        }
        const mwinCompletion* completion = &event.data.completion;
        for (uint32_t i = 0; i < s_made; i++)
        {
            if (memcmp(&s_requests[i].id, &completion->request, sizeof(mwinRequestId)) == 0)
            {
                Expect(s_requests[i].kind == completion->kind && s_requests[i].completions == 0);
                s_requests[i].completions++;
                if (completion->outcome == mwin_outcomeDone)
                {
                    CheckFound(context, completion);
                }
            }
        }
    }
}

// The next event's property, from the input: none, or a type (INCR, the
// text type, another), a format, a length up to twice the limit and a
// pattern.
static void NextProperty(void)
{
    uint8_t shape = Byte();
    static const uint8_t formats[] = {0, 8, 16, 32};
    s_server.property = (shape & 7) != 0;
    xcb_atom_t types[] = {s_platform.atoms[mwin_atomIncr], s_platform.atoms[mwin_atomUtf8String],
                          XCB_ATOM_NONE, 900};
    s_server.type = types[(shape >> 3) & 3];
    s_server.format = formats[(shape >> 5) & 3];
    uint32_t length = Byte();
    length |= (uint32_t)Byte() << 8;
    s_server.length = length % (2 * LIMIT + 8);
    for (int i = 0; i < 4; i++)
    {
        s_server.pattern[i] = Byte();
    }
}

static void Event(uint8_t op)
{
    NextProperty();
    xcb_window_t window = (op & 0x80) != 0 ? OTHER : s_platform.clipboard.window;
    xcb_generic_event_t event = {0};
    switch (((op >> 3) & 3) % 3)
    {
    case 0:
    {
        xcb_selection_notify_event_t* notify = (xcb_selection_notify_event_t*)&event;
        notify->response_type = XCB_SELECTION_NOTIFY;
        notify->requestor = window;
        notify->property = (op & 0x40) != 0 ? XCB_ATOM_NONE : s_platform.atoms[mwin_atomSelection];
        break;
    }
    case 1:
    {
        xcb_property_notify_event_t* property = (xcb_property_notify_event_t*)&event;
        property->response_type = XCB_PROPERTY_NOTIFY;
        property->window = window;
        property->atom = s_platform.atoms[mwin_atomSelection];
        property->state = (op & 0x40) != 0 ? XCB_PROPERTY_DELETE : XCB_PROPERTY_NEW_VALUE;
        break;
    }
    default:
    {
        xcb_selection_clear_event_t* clear = (xcb_selection_clear_event_t*)&event;
        clear->response_type = XCB_SELECTION_CLEAR;
        clear->owner = window;
        clear->selection =
            (op & 0x40) != 0 ? XCB_ATOM_PRIMARY : s_platform.atoms[mwin_atomClipboard];
        break;
    }
    }
    mwinX11HandleClipboardEvent(&s_platform, &event);
}

// A request of a window, from the input: the kind, the data's type, and
// what the stand-in answers interning it and taking the selection.
static void Request(mwinContext* context, mwinWindowId window)
{
    static const char* const mimes[] = {"image/png", "application/x-maul", "text/html"};
    static const char text[] = "caf\xc3\xa9";
    uint8_t shape = Byte();
    const char* mime = mimes[((shape >> 3) & 3) % 3];
    mwinRequestId id = {0};
    s_server.interned = (shape & 0x40) != 0;
    s_server.owner = (shape & 0x80) != 0;
    context->backend = &s_ops;
    switch ((shape & 7) % 6)
    {
    case 0:
        Track(context, mwinWindow_RequestClipboardRead(context, window, &id), id,
              mwin_requestClipboardRead);
        break;
    case 1:
        Track(context, mwinWindow_RequestPrimaryRead(context, window, &id), id,
              mwin_requestPrimaryRead);
        break;
    case 2:
        Track(context,
              mwinWindow_RequestClipboardReadData(context, window, mime, strlen(mime), &id), id,
              mwin_requestClipboardReadData);
        break;
    case 3:
        Track(context,
              mwinWindow_RequestClipboardWrite(context, window, text, sizeof(text) - 1, &id), id,
              mwin_requestClipboardWrite);
        break;
    case 4:
    {
        mwinClipboardItem item = {mime, strlen(mime), text, sizeof(text) - 1};
        Track(context, mwinWindow_RequestClipboardWriteData(context, window, &item, 1, &id), id,
              mwin_requestClipboardWriteData);
        break;
    }
    default:
        Track(context, mwinWindow_RequestPrimaryWrite(context, window, text, sizeof(text) - 1, &id),
              id, mwin_requestPrimaryWrite);
        break;
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    s_platform = (mwinX11Platform){.context = context, .screen = &s_screen};
    s_platform.connection = (xcb_connection_t*)&s_screen;
    for (int i = 0; i < MWIN_X11_ATOMS; i++)
    {
        s_platform.atoms[i] = (xcb_atom_t)(100 + i);
    }
    mwinX11Api* api = &s_platform.api;
    api->generateId = GenerateId;
    api->requestCheck = RequestCheck;
    api->createWindowChecked = CreateWindowChecked;
    api->destroyWindow = DestroyWindow;
    api->internAtom = InternAtom;
    api->internAtomReply = InternAtomReply;
    api->getProperty = GetProperty;
    api->getPropertyReply = GetPropertyReply;
    api->getPropertyValue = GetPropertyValue;
    api->getPropertyValueLength = GetPropertyValueLength;
    api->setSelectionOwner = SetSelectionOwner;
    api->getSelectionOwner = GetSelectionOwner;
    api->getSelectionOwnerReply = GetSelectionOwnerReply;
    api->convertSelection = ConvertSelection;
    s_test = context->backend;
    s_ops = *s_test;
    s_ops.submit = Submit;
    s_ops.now = Now;
    s_made = 0;

    mwinWindowId windows[WINDOWS];
    mwinWindowDef def = mwinDefaultWindowDef();
    for (int i = 0; i < WINDOWS; i++)
    {
        Expect(mwinCreateWindow(context, &def, &windows[i], nullptr) == mwin_success);
    }
    while (s_size > 0)
    {
        uint8_t op = Byte();
        uint32_t i = ((op >> 3) & 3) % WINDOWS;
        switch (op & 7)
        {
        case 0:
        case 1:
        case 2:
            Request(context, windows[i]);
            break;
        case 3:
        case 4:
        case 5:
            Event(op);
            break;
        case 6:
            // A read past its deadline fails, and the next starts.
            mwinX11CheckClipboard(&s_platform, UINT64_MAX);
            break;
        default:
            // A window goes while its reads may be under way.
            Expect(mwinDestroyWindow(context, windows[i]) == mwin_success &&
                   mwinCreateWindow(context, &def, &windows[i], nullptr) == mwin_success);
            break;
        }
        Drain(context);
    }
    // Each deadline fails the read under way and starts the next, so the
    // reads end.
    for (uint32_t i = 0; i <= s_made; i++)
    {
        mwinX11CheckClipboard(&s_platform, UINT64_MAX);
        Drain(context);
    }
    for (uint32_t i = 0; i < s_made; i++)
    {
        Expect(s_requests[i].completions == 1);
    }
    Expect(!s_platform.clipboard.reading);
    mwinX11StopClipboard(&s_platform);
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
    def.context.limits.clipboardBytes = LIMIT;
    def.init = Init;
    def.frame = Frame;
    Expect(mwinRun(&def) == mwin_success);
    return 0;
}
