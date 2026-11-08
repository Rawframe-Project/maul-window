// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The parts of the X server's replies and events, read only where they
// lie within the bytes libxcb holds of them. libxcb keeps a reply in its
// 32 bytes and 4 for each unit of the length the reply gives, a generic
// event's bytes past its 32 after the full sequence it adds, and the
// setup in its 8 bytes and 4 for each unit of its length. Its accessors
// find each part by the counts the server gives beside it, which nothing
// checks against those bytes. A reply whose counts put a part past its
// bytes is malformed and taken as one the server refused: released, and
// NULL in its place. A malformed event is passed over.

#ifndef MAUL_WINDOW_SRC_X11_REPLY_H
#define MAUL_WINDOW_SRC_X11_REPLY_H

#include "x11_api.h"

// Whether count items of size bytes at part lie within the first holds
// bytes at start, the arithmetic checked so that no count wraps it.
MWIN_NODISCARD bool mwinX11Holds(const void* start, uint64_t holds, const void* part,
                                 uint64_t count, uint64_t size);

// The bytes libxcb holds of a reply, a generic event and the setup.
uint64_t mwinX11ReplyBytes(const void* reply);
uint64_t mwinX11EventBytes(const xcb_ge_generic_event_t* event);
uint64_t mwinX11SetupBytes(const xcb_setup_t* setup);

// A reply with every part its accessors give within it, or NULL, the
// reply released, for one with a part past it, of a format no property
// has, or whose value passes INT_MAX bytes, which libxcb's int length
// accessors cannot carry. NULL stays NULL.
xcb_get_property_reply_t* mwinX11CheckProperty(const mwinX11Api* api,
                                               xcb_get_property_reply_t* reply);
xcb_get_atom_name_reply_t* mwinX11CheckAtomName(const mwinX11Api* api,
                                                xcb_get_atom_name_reply_t* reply);
xcb_randr_get_output_property_reply_t*
mwinX11CheckOutputProperty(const mwinX11Api* api, xcb_randr_get_output_property_reply_t* reply);
xcb_randr_get_screen_resources_current_reply_t*
mwinX11CheckResources(const mwinX11Api* api, xcb_randr_get_screen_resources_current_reply_t* reply);
xcb_randr_get_monitors_reply_t* mwinX11CheckMonitors(const mwinX11Api* api,
                                                     xcb_randr_get_monitors_reply_t* reply);
xcb_render_query_pict_formats_reply_t*
mwinX11CheckFormats(const mwinX11Api* api, xcb_render_query_pict_formats_reply_t* reply);
xcb_input_list_input_devices_reply_t*
mwinX11CheckInputDevices(const mwinX11Api* api, xcb_input_list_input_devices_reply_t* reply);

// A QueryFont reply that holds its fields up to its list of character
// infos, or NULL, the reply released.
xcb_query_font_reply_t* mwinX11CheckFont(xcb_query_font_reply_t* reply);

// The bytes of a checked property's value and of a checked output
// property's data, in size_t from the counts the reply gives, never
// through libxcb's int length accessors.
size_t mwinX11PropertyBytes(const xcb_get_property_reply_t* reply);
size_t mwinX11OutputPropertyBytes(const xcb_randr_get_output_property_reply_t* reply);

// An XIQueryDevice reply whose device infos, their names and their
// classes lie within it, each class at least as long as the fields of
// its type the backend reads, or NULL, the reply released. libxcb's
// iterators step a class by sizes it reckons from the class's own
// fields, so the classes are walked here by the length each gives, as
// the protocol defines it.
xcb_input_xi_query_device_reply_t* mwinX11CheckDevices(xcb_input_xi_query_device_reply_t* reply);

// The device infos of a checked reply, and an info's classes, walked
// as libxcb's iterators are, by the length each gives.
typedef struct mwinX11Devices
{
    const xcb_input_xi_device_info_t* data;
    uint16_t rem;
} mwinX11Devices;

typedef struct mwinX11Classes
{
    const xcb_input_device_class_t* data;
    uint16_t rem;
} mwinX11Classes;

mwinX11Devices mwinX11DevicesOf(const xcb_input_xi_query_device_reply_t* reply);
void mwinX11NextDevice(mwinX11Devices* devices);
mwinX11Classes mwinX11ClassesOf(const xcb_input_xi_device_info_t* info);
void mwinX11NextClass(mwinX11Classes* classes);
// An info's name, of its name_len bytes.
const char* mwinX11DeviceName(const xcb_input_xi_device_info_t* info);

// Whether an XInput event holds the fields of its type the backend
// reads, and its valuators: the mask and the values the mask names.
MWIN_NODISCARD bool mwinX11XiEventWhole(const mwinX11Api* api, const xcb_ge_generic_event_t* event);

// The screen of a number in the setup, NULL where the setup does not
// hold it whole, nor every screen before it.
const xcb_screen_t* mwinX11ScreenOf(const mwinX11Api* api, const xcb_setup_t* setup, int number);

#endif // MAUL_WINDOW_SRC_X11_REPLY_H
