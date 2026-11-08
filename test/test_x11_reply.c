// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X server's replies and events read only within their bytes
// (src/x11_reply.c), on replies built by hand and libxcb's own
// accessors, with no server:
// - a part within the bytes, one past them by a count, and a count so
//   large it would wrap, for a property, an atom's name, an output
//   property, the screen resources' modes, the monitors and their
//   outputs, the picture formats and the input devices; a property of a
//   format no property has; a property and an output property of 2 GiB
//   or more, within the bytes a reply claims, which libxcb's int length
//   cannot give;
// - a QueryFont reply that ends before the ascent and descent the
//   message box reads;
// - XIQueryDevice's infos, names and classes, a class shorter than its
//   type's fields or than any class, and the walk by the lengths they
//   give, past a class of a type libxcb does not know;
// - XInput events: a device event's and a raw event's fields, mask and
//   values, an event shorter than its fields, and the events whose
//   fields are not read;
// - the setup's screens, their depths and visuals;
// - a malformed reply released and NULL, which ASan and the leak check
//   see.

#include "test_harness.h"
#include "x11_reply.h"

#include <stdlib.h>
#include <string.h>

static mwinX11Api s_api;

// A reply or event of so many bytes, zeroed, its length field set from
// the bytes past head.
static void* Built(size_t bytes, size_t head)
{
    uint8_t* reply = calloc(1, bytes);
    uint32_t length = (uint32_t)((bytes - head) / 4);
    memcpy(reply + 4, &length, sizeof(length));
    return reply;
}

static void* Reply(size_t bytes)
{
    return Built(bytes, 32);
}

// A short reply whose length field claims 2 GiB past its 32 bytes, as
// one that held them would: only a check that is not of the bytes held
// can refuse a count within them.
static void* Huge(void)
{
    uint8_t* reply = Reply(36);
    uint32_t length = 0x20000000u;
    memcpy(reply + 4, &length, sizeof(length));
    return reply;
}

static void CheckHolds(void)
{
    // Room past the 16 bytes held, for a part past them.
    uint8_t bytes[32];
    CHECK(mwinX11Holds(bytes, 16, bytes + 8, 2, 4) && !mwinX11Holds(bytes, 16, bytes + 8, 3, 4) &&
              mwinX11Holds(bytes, 16, bytes + 16, 0, 4) && !mwinX11Holds(bytes, 16, bytes, 1, 17),
          "a part within the bytes, and one past them");
    CHECK(!mwinX11Holds(bytes, 16, bytes, UINT64_MAX, 8) &&
              !mwinX11Holds(bytes + 8, 8, bytes, 1, 1) &&
              !mwinX11Holds(bytes, 16, bytes + 17, 0, 1) && !mwinX11Holds(nullptr, 16, bytes, 0, 1),
          "a count that would wrap, a part before the start or past the end, and no start");
}

static void CheckProperty(void)
{
    xcb_get_property_reply_t* reply = Reply(36);
    reply->format = 8;
    reply->value_len = 4;
    reply = mwinX11CheckProperty(&s_api, reply);
    CHECK(reply != nullptr && mwinX11PropertyBytes(reply) == 4,
          "a property's value within its bytes");
    free(reply);
    reply = Reply(36);
    reply->format = 32;
    reply->value_len = 2;
    CHECK(mwinX11CheckProperty(&s_api, reply) == nullptr, "a property's value past its bytes");
    reply = Reply(36);
    reply->format = 32;
    reply->value_len = 0x40000001u;
    CHECK(mwinX11CheckProperty(&s_api, reply) == nullptr, "a property's count past its bytes");
    // 2 GiB of value, within the bytes the reply claims: libxcb's int
    // length would be negative.
    reply = Huge();
    reply->format = 32;
    reply->value_len = 0x20000000u;
    CHECK(mwinX11CheckProperty(&s_api, reply) == nullptr,
          "a property of INT_MAX bytes or more, within its claimed bytes");
    reply = Reply(36);
    reply->format = 7;
    CHECK(mwinX11CheckProperty(&s_api, reply) == nullptr, "a property of a format none has");
    CHECK(mwinX11CheckProperty(&s_api, nullptr) == nullptr, "no reply stays none");
}

static void CheckAtomName(void)
{
    xcb_get_atom_name_reply_t* reply = Reply(36);
    reply->name_len = 4;
    reply = mwinX11CheckAtomName(&s_api, reply);
    CHECK(reply != nullptr, "an atom's name within its bytes");
    free(reply);
    reply = Reply(36);
    reply->name_len = 5;
    CHECK(mwinX11CheckAtomName(&s_api, reply) == nullptr, "an atom's name past its bytes");
}

static void CheckOutputProperty(void)
{
    xcb_randr_get_output_property_reply_t* reply = Reply(32 + 128);
    reply->format = 8;
    reply->num_items = 128;
    reply = mwinX11CheckOutputProperty(&s_api, reply);
    CHECK(reply != nullptr, "an output property within its bytes");
    free(reply);
    reply = Reply(32 + 128);
    reply->format = 8;
    reply->num_items = 256 * 128;
    CHECK(mwinX11CheckOutputProperty(&s_api, reply) == nullptr,
          "an EDID of 256 blocks in a reply of one");
    reply = Huge();
    reply->format = 8;
    reply->num_items = 0x80000000u;
    CHECK(mwinX11CheckOutputProperty(&s_api, reply) == nullptr,
          "an output property of INT_MAX bytes or more, within its claimed bytes");
}

// A QueryFont reply: the message box reads its ascent and descent, at
// bytes 52 to 55.
static void CheckFont(void)
{
    xcb_query_font_reply_t* reply = Reply(sizeof(xcb_query_font_reply_t));
    reply->font_ascent = 12;
    reply = mwinX11CheckFont(reply);
    CHECK(reply != nullptr && reply->font_ascent == 12, "a font's metrics within the reply");
    free(reply);
    CHECK(mwinX11CheckFont(Reply(32)) == nullptr, "a font reply of no length past its 32 bytes");
    CHECK(mwinX11CheckFont(Reply(52)) == nullptr, "a font reply that ends before its ascent");
    CHECK(mwinX11CheckFont(nullptr) == nullptr, "no font reply stays none");
}

static void CheckResources(void)
{
    // A CRTC, an output and a mode.
    size_t bytes = sizeof(xcb_randr_get_screen_resources_current_reply_t) + 4 + 4 +
                   sizeof(xcb_randr_mode_info_t);
    xcb_randr_get_screen_resources_current_reply_t* reply = Reply(bytes);
    reply->num_crtcs = 1;
    reply->num_outputs = 1;
    reply->num_modes = 1;
    reply = mwinX11CheckResources(&s_api, reply);
    CHECK(reply != nullptr, "the modes within the bytes");
    free(reply);
    reply = Reply(bytes);
    reply->num_crtcs = 1;
    reply->num_outputs = 2;
    reply->num_modes = 1;
    CHECK(mwinX11CheckResources(&s_api, reply) == nullptr,
          "an output more than the bytes hold, which puts the modes past them");
}

static void CheckMonitors(void)
{
    size_t one = sizeof(xcb_randr_monitor_info_t) + sizeof(xcb_randr_output_t);
    size_t bytes = sizeof(xcb_randr_get_monitors_reply_t) + 2 * one;
    xcb_randr_get_monitors_reply_t* reply = Reply(bytes);
    reply->nMonitors = 2;
    xcb_randr_monitor_info_t* first = (xcb_randr_monitor_info_t*)(reply + 1);
    first->nOutput = 1;
    xcb_randr_monitor_info_t* second = (xcb_randr_monitor_info_t*)((uint8_t*)first + one);
    second->nOutput = 1;
    reply = mwinX11CheckMonitors(&s_api, reply);
    CHECK(reply != nullptr, "two monitors and their outputs within the bytes");
    free(reply);
    reply = Reply(bytes);
    reply->nMonitors = 2;
    ((xcb_randr_monitor_info_t*)(reply + 1))->nOutput = 1;
    second = (xcb_randr_monitor_info_t*)((uint8_t*)(reply + 1) + one);
    second->nOutput = 2;
    CHECK(mwinX11CheckMonitors(&s_api, reply) == nullptr, "the last monitor's outputs past them");
    reply = Reply(bytes);
    reply->nMonitors = 3;
    ((xcb_randr_monitor_info_t*)(reply + 1))->nOutput = 1;
    ((xcb_randr_monitor_info_t*)((uint8_t*)(reply + 1) + one))->nOutput = 1;
    CHECK(mwinX11CheckMonitors(&s_api, reply) == nullptr, "a monitor past the bytes");
    reply = Reply(bytes);
    reply->nMonitors = 2;
    ((xcb_randr_monitor_info_t*)(reply + 1))->nOutput = UINT16_MAX;
    CHECK(mwinX11CheckMonitors(&s_api, reply) == nullptr,
          "a monitor's outputs past them, its step not taken");
}

static void CheckFormats(void)
{
    size_t bytes =
        sizeof(xcb_render_query_pict_formats_reply_t) + sizeof(xcb_render_pictforminfo_t);
    xcb_render_query_pict_formats_reply_t* reply = Reply(bytes);
    reply->num_formats = 1;
    reply = mwinX11CheckFormats(&s_api, reply);
    CHECK(reply != nullptr, "a picture format within the bytes");
    free(reply);
    reply = Reply(bytes);
    reply->num_formats = 2;
    CHECK(mwinX11CheckFormats(&s_api, reply) == nullptr, "a picture format past them");
}

static void CheckInputDevices(void)
{
    size_t bytes = sizeof(xcb_input_list_input_devices_reply_t) + sizeof(xcb_input_device_info_t);
    xcb_input_list_input_devices_reply_t* reply = Reply(bytes);
    reply->devices_len = 1;
    reply = mwinX11CheckInputDevices(&s_api, reply);
    CHECK(reply != nullptr, "an input device within the bytes");
    free(reply);
    reply = Reply(bytes);
    reply->devices_len = 2;
    CHECK(mwinX11CheckInputDevices(&s_api, reply) == nullptr, "an input device past them");
}

// An XIQueryDevice reply: a device named "pen" with a valuator class,
// then a device of id 9 with a class of a type libxcb does not know.
#define PEN_INFO   (sizeof(xcb_input_xi_device_info_t) + 4)
#define VALUATOR   sizeof(xcb_input_valuator_class_t)
#define OTHER_INFO (sizeof(xcb_input_xi_device_info_t) + 8)
#define DEVICES    (sizeof(xcb_input_xi_query_device_reply_t) + PEN_INFO + VALUATOR + OTHER_INFO)

static xcb_input_xi_query_device_reply_t* Devices(void)
{
    xcb_input_xi_query_device_reply_t* reply = Reply(DEVICES);
    reply->num_infos = 2;
    uint8_t* at = (uint8_t*)(reply + 1);
    xcb_input_xi_device_info_t* pen = (xcb_input_xi_device_info_t*)at;
    *pen = (xcb_input_xi_device_info_t){.deviceid = 4, .num_classes = 1, .name_len = 3};
    memcpy(pen + 1, "pen", 3);
    xcb_input_valuator_class_t* valuator = (xcb_input_valuator_class_t*)(at + PEN_INFO);
    *valuator = (xcb_input_valuator_class_t){
        .type = XCB_INPUT_DEVICE_CLASS_TYPE_VALUATOR, .len = VALUATOR / 4, .number = 2};
    xcb_input_xi_device_info_t* other = (xcb_input_xi_device_info_t*)(at + PEN_INFO + VALUATOR);
    *other = (xcb_input_xi_device_info_t){.deviceid = 9, .num_classes = 1};
    xcb_input_device_class_t* unknown = (xcb_input_device_class_t*)(other + 1);
    *unknown = (xcb_input_device_class_t){.type = 0x7777, .len = 2};
    return reply;
}

static void CheckDevices(void)
{
    xcb_input_xi_query_device_reply_t* reply = mwinX11CheckDevices(Devices());
    CHECK(reply != nullptr, "two devices, their names and classes within the bytes");
    if (reply != nullptr)
    {
        mwinX11Devices devices = mwinX11DevicesOf(reply);
        const xcb_input_xi_device_info_t* pen = devices.data;
        mwinX11Classes classes = mwinX11ClassesOf(pen);
        const xcb_input_valuator_class_t* valuator =
            (const xcb_input_valuator_class_t*)classes.data;
        mwinX11NextDevice(&devices);
        CHECK(pen->deviceid == 4 && memcmp(mwinX11DeviceName(pen), "pen", 3) == 0 &&
                  classes.rem == 1 && valuator->number == 2 && devices.rem == 1 &&
                  devices.data->deviceid == 9 &&
                  mwinX11ClassesOf(devices.data).data->type == 0x7777,
              "the walk by the lengths given, past a class of a type libxcb does not know");
    }
    free(reply);

    reply = Devices();
    ((xcb_input_valuator_class_t*)((uint8_t*)(reply + 1) + PEN_INFO))->len = VALUATOR / 4 - 1;
    CHECK(mwinX11CheckDevices(reply) == nullptr, "a valuator class shorter than its fields");
    reply = Devices();
    ((xcb_input_valuator_class_t*)((uint8_t*)(reply + 1) + PEN_INFO))->len = 0;
    CHECK(mwinX11CheckDevices(reply) == nullptr, "a class of no length");
    reply = Devices();
    ((xcb_input_xi_device_info_t*)(reply + 1))->name_len = 200;
    CHECK(mwinX11CheckDevices(reply) == nullptr, "a name past the bytes");
    reply = Devices();
    reply->num_infos = 3;
    CHECK(mwinX11CheckDevices(reply) == nullptr, "a device past the bytes");
    reply = Devices();
    ((xcb_input_xi_device_info_t*)((uint8_t*)(reply + 1) + PEN_INFO + VALUATOR))->num_classes = 2;
    CHECK(mwinX11CheckDevices(reply) == nullptr, "a class past the bytes");
}

// A generic event of so many bytes, its type and extension set.
static xcb_ge_generic_event_t* Event(size_t bytes, uint16_t type)
{
    xcb_ge_generic_event_t* event = Built(bytes, sizeof(xcb_ge_generic_event_t));
    event->response_type = XCB_GE_GENERIC;
    event->event_type = type;
    return event;
}

static void CheckEvents(void)
{
    // One word of mask, two valuators set: two values.
    size_t device = sizeof(xcb_input_button_press_event_t) + 4 + 2 * sizeof(xcb_input_fp3232_t);
    xcb_ge_generic_event_t* event = Event(device, XCB_INPUT_BUTTON_PRESS);
    xcb_input_button_press_event_t* press = (xcb_input_button_press_event_t*)event;
    press->valuators_len = 1;
    uint32_t mask = 0x3u;
    memcpy(s_api.valuatorMask(press), &mask, sizeof(mask));
    CHECK(mwinX11XiEventWhole(&s_api, event), "a device event's mask and values within it");
    mask = 0x7u;
    memcpy(s_api.valuatorMask(press), &mask, sizeof(mask));
    CHECK(!mwinX11XiEventWhole(&s_api, event), "a value past it");
    press->valuators_len = 100;
    CHECK(!mwinX11XiEventWhole(&s_api, event), "a mask past it");
    free(event);
    event = Event(sizeof(xcb_ge_generic_event_t), XCB_INPUT_TOUCH_BEGIN);
    CHECK(!mwinX11XiEventWhole(&s_api, event), "a touch event shorter than its fields");
    free(event);

    // Raw: the values and the raw values, two each.
    size_t raw = sizeof(xcb_input_raw_button_press_event_t) + 4 + 4 * sizeof(xcb_input_fp3232_t);
    event = Event(raw, XCB_INPUT_RAW_MOTION);
    xcb_input_raw_button_press_event_t* motion = (xcb_input_raw_button_press_event_t*)event;
    motion->valuators_len = 1;
    mask = 0x3u;
    memcpy(s_api.rawValuatorMask(motion), &mask, sizeof(mask));
    CHECK(mwinX11XiEventWhole(&s_api, event), "a raw event's mask and values within it");
    mask = 0x7u;
    memcpy(s_api.rawValuatorMask(motion), &mask, sizeof(mask));
    CHECK(!mwinX11XiEventWhole(&s_api, event), "a raw value past it");
    free(event);

    event = Event(sizeof(xcb_ge_generic_event_t), XCB_INPUT_DEVICE_CHANGED);
    xcb_ge_generic_event_t* hierarchy = Event(sizeof(xcb_ge_generic_event_t), XCB_INPUT_HIERARCHY);
    CHECK(mwinX11XiEventWhole(&s_api, event) && mwinX11XiEventWhole(&s_api, hierarchy),
          "a device change within its 32 bytes, and an event whose fields are not read");
    free(hierarchy);
    free(event);
}

// A setup of one screen of one depth of one visual.
#define SETUP_BYTES                                                                                \
    (sizeof(xcb_setup_t) + sizeof(xcb_screen_t) + sizeof(xcb_depth_t) + sizeof(xcb_visualtype_t))

static void CheckSetup(void)
{
    uint32_t words[SETUP_BYTES / 4] = {0};
    xcb_setup_t* setup = (xcb_setup_t*)words;
    setup->length = (uint16_t)((SETUP_BYTES - 8) / 4);
    setup->roots_len = 1;
    xcb_screen_t* screen = (xcb_screen_t*)(setup + 1);
    screen->root = 0x123;
    screen->allowed_depths_len = 1;
    xcb_depth_t* depth = (xcb_depth_t*)(screen + 1);
    depth->visuals_len = 1;
    const xcb_screen_t* found = mwinX11ScreenOf(&s_api, setup, 0);
    CHECK(found != nullptr && found->root == 0x123 && mwinX11ScreenOf(&s_api, setup, 1) == nullptr,
          "the first screen within the setup, and none past the screens it has");
    setup->roots_len = 2;
    CHECK(mwinX11ScreenOf(&s_api, setup, 0) != nullptr &&
              mwinX11ScreenOf(&s_api, setup, 1) == nullptr,
          "a screen past the setup's bytes");
    setup->roots_len = 1;
    depth->visuals_len = 2;
    CHECK(mwinX11ScreenOf(&s_api, setup, 0) == nullptr, "a screen whose visuals pass the setup");
    depth->visuals_len = 1;
    setup->vendor_len = 200;
    CHECK(mwinX11ScreenOf(&s_api, setup, 0) == nullptr, "a vendor that puts the screen past it");
}

int main(void)
{
    if (mwinLoadX11(&s_api) != mwin_success || s_api.randrLibrary == nullptr ||
        s_api.renderLibrary == nullptr || s_api.xinputLibrary == nullptr)
    {
        mwinUnloadX11(&s_api);
        // hygiene: a machine without the XCB libraries is a skip; CI's image has them
        return 77;
    }
    CheckHolds();
    CheckProperty();
    CheckAtomName();
    CheckOutputProperty();
    CheckFont();
    CheckResources();
    CheckMonitors();
    CheckFormats();
    CheckInputDevices();
    CheckDevices();
    CheckEvents();
    CheckSetup();
    mwinUnloadX11(&s_api);
    return s_failures == 0 ? 0 : 1;
}
