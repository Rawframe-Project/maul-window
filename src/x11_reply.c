// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The parts of the X server's replies and events, within their bytes.

#include "x11_reply.h"

#include "allocator.h"

#include <string.h>

// A reply's and a generic event's own bytes, the length beside them, and
// the setup's.
#define REPLY_HEAD 32u
#define SETUP_HEAD 8u

// A value whose bytes may lie at any alignment.
static uint32_t Word(const void* at)
{
    uint32_t value;
    memcpy(&value, at, sizeof(value));
    return value;
}

bool mwinX11Holds(const void* start, uint64_t holds, const void* part, uint64_t count,
                  uint64_t size)
{
    uintptr_t from = (uintptr_t)start;
    uintptr_t at = (uintptr_t)part;
    if (start == nullptr || part == nullptr || at < from || at - from > holds)
    {
        return false;
    }
    uint64_t left = holds - (uint64_t)(at - from);
    return size == 0 || count <= left / size;
}

uint64_t mwinX11ReplyBytes(const void* reply)
{
    return REPLY_HEAD + (uint64_t)Word((const uint8_t*)reply + 4) * 4u;
}

uint64_t mwinX11EventBytes(const xcb_ge_generic_event_t* event)
{
    // libxcb puts the bytes past the 32 after the full sequence it adds.
    return sizeof(xcb_ge_generic_event_t) + (uint64_t)event->length * 4u;
}

uint64_t mwinX11SetupBytes(const xcb_setup_t* setup)
{
    return SETUP_HEAD + (uint64_t)setup->length * 4u;
}

// Whether count items of size bytes at part lie within the reply.
MWIN_NODISCARD static bool InReply(const void* reply, const void* part, uint64_t count,
                                   uint64_t size)
{
    return mwinX11Holds(reply, mwinX11ReplyBytes(reply), part, count, size);
}

// The reply where it is whole, else NULL, the reply released.
static void* Kept(void* reply, bool whole)
{
    if (whole)
    {
        return reply;
    }
    mwinReleaseSystemMemory(reply);
    return nullptr;
}

// The formats a property's value may have: none for no property, or
// items of 8, 16 or 32 bits.
MWIN_NODISCARD static bool IsFormat(uint8_t format)
{
    return format == 0 || format == 8 || format == 16 || format == 32;
}

xcb_get_property_reply_t* mwinX11CheckProperty(const mwinX11Api* api,
                                               xcb_get_property_reply_t* reply)
{
    return reply == nullptr ? nullptr
                            : Kept(reply, IsFormat(reply->format) &&
                                              InReply(reply, api->getPropertyValue(reply),
                                                      reply->value_len, reply->format / 8u));
}

xcb_get_atom_name_reply_t* mwinX11CheckAtomName(const mwinX11Api* api,
                                                xcb_get_atom_name_reply_t* reply)
{
    return reply == nullptr
               ? nullptr
               : Kept(reply, InReply(reply, api->getAtomNameName(reply), reply->name_len, 1));
}

xcb_randr_get_output_property_reply_t*
mwinX11CheckOutputProperty(const mwinX11Api* api, xcb_randr_get_output_property_reply_t* reply)
{
    return reply == nullptr ? nullptr
                            : Kept(reply, IsFormat(reply->format) &&
                                              InReply(reply, api->randrOutputPropertyData(reply),
                                                      reply->num_items, reply->format / 8u));
}

xcb_randr_get_screen_resources_current_reply_t*
mwinX11CheckResources(const mwinX11Api* api, xcb_randr_get_screen_resources_current_reply_t* reply)
{
    // The modes follow the CRTCs and the outputs, so holding them holds
    // those too.
    return reply == nullptr ? nullptr
                            : Kept(reply, InReply(reply, api->randrResourceModes(reply),
                                                  reply->num_modes, sizeof(xcb_randr_mode_info_t)));
}

xcb_randr_get_monitors_reply_t* mwinX11CheckMonitors(const mwinX11Api* api,
                                                     xcb_randr_get_monitors_reply_t* reply)
{
    if (reply == nullptr)
    {
        return nullptr;
    }
    // Each step goes by the count of outputs of a monitor just checked,
    // and none is taken past one that is not whole.
    xcb_randr_monitor_info_iterator_t it = api->randrMonitorsIterator(reply);
    bool whole = true;
    while (whole && it.rem > 0)
    {
        whole = InReply(reply, it.data, 1, sizeof(*it.data)) &&
                InReply(reply, api->randrMonitorOutputs(it.data), it.data->nOutput,
                        sizeof(xcb_randr_output_t));
        if (whole)
        {
            api->randrMonitorInfoNext(&it);
        }
    }
    return Kept(reply, whole);
}

xcb_render_query_pict_formats_reply_t*
mwinX11CheckFormats(const mwinX11Api* api, xcb_render_query_pict_formats_reply_t* reply)
{
    return reply == nullptr
               ? nullptr
               : Kept(reply, InReply(reply, api->renderFormats(reply), reply->num_formats,
                                     sizeof(xcb_render_pictforminfo_t)));
}

xcb_input_list_input_devices_reply_t*
mwinX11CheckInputDevices(const mwinX11Api* api, xcb_input_list_input_devices_reply_t* reply)
{
    return reply == nullptr
               ? nullptr
               : Kept(reply, InReply(reply, api->inputDevices(reply), reply->devices_len,
                                     sizeof(xcb_input_device_info_t)));
}

// The bytes a class's fields of its type take, which the backend reads.
static uint64_t FieldsOf(uint16_t type)
{
    switch (type)
    {
    case XCB_INPUT_DEVICE_CLASS_TYPE_VALUATOR:
        return sizeof(xcb_input_valuator_class_t);
    case XCB_INPUT_DEVICE_CLASS_TYPE_SCROLL:
        return sizeof(xcb_input_scroll_class_t);
    case XCB_INPUT_DEVICE_CLASS_TYPE_TOUCH:
        return sizeof(xcb_input_touch_class_t);
    default:
        return sizeof(xcb_input_device_class_t);
    }
}

// The bytes a device info takes before its classes: its fields and its
// name, padded to 4.
static uint64_t HeadOf(const xcb_input_xi_device_info_t* info)
{
    return sizeof(*info) + ((uint64_t)info->name_len + 3u) / 4u * 4u;
}

xcb_input_xi_query_device_reply_t* mwinX11CheckDevices(xcb_input_xi_query_device_reply_t* reply)
{
    if (reply == nullptr)
    {
        return nullptr;
    }
    // Each step goes by a length just checked, an info's fields and name,
    // then each class's, and none is taken past a part that is not whole.
    mwinX11Devices devices = mwinX11DevicesOf(reply);
    bool whole = true;
    while (whole && devices.rem > 0)
    {
        const xcb_input_xi_device_info_t* info = devices.data;
        whole = InReply(reply, info, 1, sizeof(*info)) && InReply(reply, info, 1, HeadOf(info));
        mwinX11Classes classes = whole ? mwinX11ClassesOf(info) : (mwinX11Classes){0};
        while (whole && classes.rem > 0)
        {
            const xcb_input_device_class_t* item = classes.data;
            whole = InReply(reply, item, 1, sizeof(*item)) &&
                    (uint64_t)item->len * 4u >= FieldsOf(item->type) &&
                    InReply(reply, item, item->len, 4);
            if (whole)
            {
                mwinX11NextClass(&classes);
            }
        }
        if (whole)
        {
            // Past its last class.
            devices.data = (const xcb_input_xi_device_info_t*)classes.data;
            devices.rem -= 1;
        }
    }
    return Kept(reply, whole);
}

mwinX11Devices mwinX11DevicesOf(const xcb_input_xi_query_device_reply_t* reply)
{
    return (mwinX11Devices){(const xcb_input_xi_device_info_t*)(reply + 1), reply->num_infos};
}

mwinX11Classes mwinX11ClassesOf(const xcb_input_xi_device_info_t* info)
{
    return (mwinX11Classes){(const xcb_input_device_class_t*)((const uint8_t*)info + HeadOf(info)),
                            info->num_classes};
}

void mwinX11NextClass(mwinX11Classes* classes)
{
    classes->data = (const xcb_input_device_class_t*)((const uint8_t*)classes->data +
                                                      (size_t)classes->data->len * 4u);
    classes->rem -= 1;
}

void mwinX11NextDevice(mwinX11Devices* devices)
{
    mwinX11Classes classes = mwinX11ClassesOf(devices->data);
    while (classes.rem > 0)
    {
        mwinX11NextClass(&classes);
    }
    // Past the last class is the next info.
    devices->data = (const xcb_input_xi_device_info_t*)classes.data;
    devices->rem -= 1;
}

const char* mwinX11DeviceName(const xcb_input_xi_device_info_t* info)
{
    return (const char*)(info + 1);
}

// Whether a generic event holds count items of size bytes at part.
MWIN_NODISCARD static bool InEvent(const xcb_ge_generic_event_t* event, const void* part,
                                   uint64_t count, uint64_t size)
{
    return mwinX11Holds(event, mwinX11EventBytes(event), part, count, size);
}

bool mwinX11XiEventWhole(const mwinX11Api* api, const xcb_ge_generic_event_t* event)
{
    switch (event->event_type)
    {
    case XCB_INPUT_RAW_MOTION:
    {
        // The raw values follow the mask and the values, so holding them
        // holds those too.
        const xcb_input_raw_button_press_event_t* raw =
            (const xcb_input_raw_button_press_event_t*)event;
        return InEvent(event, raw, 1, sizeof(*raw)) &&
               InEvent(event, api->rawValuatorMask(raw), raw->valuators_len, sizeof(uint32_t)) &&
               InEvent(event, api->rawAxisValues(raw), (uint64_t)api->rawAxisValuesLength(raw),
                       sizeof(xcb_input_fp3232_t));
    }
    case XCB_INPUT_MOTION:
    case XCB_INPUT_BUTTON_PRESS:
    case XCB_INPUT_BUTTON_RELEASE:
    case XCB_INPUT_TOUCH_BEGIN:
    case XCB_INPUT_TOUCH_UPDATE:
    case XCB_INPUT_TOUCH_END:
    {
        // Touch events have the layout of a button event.
        const xcb_input_button_press_event_t* device = (const xcb_input_button_press_event_t*)event;
        return InEvent(event, device, 1, sizeof(*device)) &&
               InEvent(event, api->valuatorMask(device), device->valuators_len, sizeof(uint32_t)) &&
               InEvent(event, api->axisValues(device), (uint64_t)api->axisValuesLength(device),
                       sizeof(xcb_input_fp3232_t));
    }
    case XCB_INPUT_DEVICE_CHANGED:
        return InEvent(event, event, 1, sizeof(xcb_input_device_changed_event_t));
    default:
        return true;
    }
}

// The bytes a screen takes: its fields, then each depth's and its
// visuals'.
MWIN_NODISCARD static bool ScreenFits(const xcb_setup_t* setup, const xcb_screen_t* screen,
                                      uint64_t* bytesOut)
{
    uint64_t holds = mwinX11SetupBytes(setup);
    if (!mwinX11Holds(setup, holds, screen, 1, sizeof(*screen)))
    {
        return false;
    }
    const uint8_t* at = (const uint8_t*)(screen + 1);
    for (uint8_t i = 0; i < screen->allowed_depths_len; i++)
    {
        const xcb_depth_t* depth = (const xcb_depth_t*)at;
        if (!mwinX11Holds(setup, holds, depth, 1, sizeof(*depth)) ||
            !mwinX11Holds(setup, holds, depth + 1, depth->visuals_len, sizeof(xcb_visualtype_t)))
        {
            return false;
        }
        at = (const uint8_t*)(depth + 1) + (size_t)depth->visuals_len * sizeof(xcb_visualtype_t);
    }
    *bytesOut = (uint64_t)(at - (const uint8_t*)screen);
    return true;
}

const xcb_screen_t* mwinX11ScreenOf(const mwinX11Api* api, const xcb_setup_t* setup, int number)
{
    if (setup == nullptr || number < 0 ||
        !mwinX11Holds(setup, mwinX11SetupBytes(setup), setup, 1, sizeof(*setup)))
    {
        return nullptr;
    }
    xcb_screen_iterator_t screens = api->setupRootsIterator(setup);
    const uint8_t* at = (const uint8_t*)screens.data;
    for (int i = 0; i < screens.rem; i++)
    {
        uint64_t bytes = 0;
        if (!ScreenFits(setup, (const xcb_screen_t*)at, &bytes))
        {
            return nullptr;
        }
        if (i == number)
        {
            return (const xcb_screen_t*)at;
        }
        at += bytes;
    }
    return nullptr;
}
