// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The other client the X11 clipboard tests talk to, a second XCB
// connection of the test's: it owns a selection with bytes of one type,
// served whole or in pieces (INCR), and reads a selection's target,
// taking pieces as they come.

#ifndef MAUL_WINDOW_TEST_X11_PEER_H
#define MAUL_WINDOW_TEST_X11_PEER_H

#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

// The largest piece either side sends at once.
#define PEER_PIECE (64u * 1024u)

// The other client: its window and atoms; the selection it owns, the
// type and bytes it serves, and a transfer of them in pieces; and what
// its last read got, into a buffer of the test's.
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
    xcb_atom_t type;
    const char* text;
    size_t length;
    xcb_window_t sendTo;
    xcb_atom_t sendProperty;
    size_t sendOffset;
    bool incremental;
    bool gotAll;
    bool refused;
    xcb_atom_t gotType;
    char* got;
    size_t gotLength;
} Peer;

static inline xcb_atom_t Intern(xcb_connection_t* connection, const char* name)
{
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name), nullptr);
    xcb_atom_t atom = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

static inline bool PeerStart(Peer* peer)
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

// Asks the owner of a selection for a target.
static inline void PeerConvert(Peer* peer, xcb_atom_t selection, xcb_atom_t target)
{
    peer->gotLength = 0;
    peer->gotAll = false;
    peer->refused = false;
    peer->incremental = false;
    xcb_convert_selection(peer->connection, peer->window, selection, target, peer->property,
                          XCB_CURRENT_TIME);
    xcb_flush(peer->connection);
}

// Takes a selection with bytes of a type, or gives it to no one.
static inline void PeerOwn(Peer* peer, xcb_atom_t selection, xcb_atom_t type, const char* text,
                           size_t length)
{
    peer->type = type;
    peer->text = text;
    peer->length = length;
    peer->sendTo = 0;
    xcb_set_selection_owner(peer->connection, text != nullptr ? peer->window : XCB_WINDOW_NONE,
                            selection, XCB_CURRENT_TIME);
    xcb_flush(peer->connection);
}

// Takes the property of the peer's read: false when it was empty.
static inline bool PeerTake(Peer* peer)
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

static inline void PeerNotify(Peer* peer, const xcb_selection_request_event_t* request,
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

// Serves the peer's bytes as their type, whole or in pieces; refuses
// any other target.
static inline void PeerServe(Peer* peer, const xcb_selection_request_event_t* request)
{
    if (request->target != peer->type)
    {
        PeerNotify(peer, request, XCB_ATOM_NONE);
        return;
    }
    if (peer->length <= PEER_PIECE)
    {
        xcb_change_property(peer->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            request->property, peer->type, 8, (uint32_t)peer->length, peer->text);
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
        peer->sendOffset = 0;
    }
    PeerNotify(peer, request, request->property);
}

static inline void PeerProperty(Peer* peer, const xcb_property_notify_event_t* event)
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
        uint32_t length = (uint32_t)(left < PEER_PIECE ? left : PEER_PIECE);
        xcb_change_property(peer->connection, XCB_PROP_MODE_REPLACE, peer->sendTo,
                            peer->sendProperty, peer->type, 8, length,
                            peer->text + peer->sendOffset);
        peer->sendOffset += length;
        peer->sendTo = length == 0 ? 0 : peer->sendTo;
    }
}

static inline void PeerPump(Peer* peer)
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

// Whether the peer's last read, a TARGETS list, names an atom.
static inline bool PeerGotTarget(const Peer* peer, xcb_atom_t target)
{
    for (size_t i = 0; i + sizeof(xcb_atom_t) <= peer->gotLength; i += sizeof(xcb_atom_t))
    {
        xcb_atom_t atom = 0;
        memcpy(&atom, peer->got + i, sizeof(atom));
        if (atom == target)
        {
            return true;
        }
    }
    return false;
}

#endif // MAUL_WINDOW_TEST_X11_PEER_H
