// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 keyboard and pointer input.

#include "x11_input.h"

#include "allocator.h"
#include "evdev.h"
#include "xkb_keyboard.h"

// X11 key codes are evdev codes plus 8.
#define XKB_OFFSET 8

// Reads the keymap and state of the keyboard device anew.
static bool ReadKeymap(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    mwinX11Keyboard* keyboard = &platform->keyboard;
    struct xkb_keymap* keymap = api->xkbKeymapFromDevice(
        keyboard->xkb.context, platform->connection, keyboard->device, XKB_KEYMAP_COMPILE_NO_FLAGS);
    struct xkb_state* state =
        keymap != nullptr ? api->xkbStateFromDevice(keymap, platform->connection, keyboard->device)
                          : nullptr;
    if (state == nullptr)
    {
        if (keymap != nullptr)
        {
            platform->xkbApi.keymapUnref(keymap);
        }
        return false;
    }
    mwinXkbSetKeymap(&keyboard->xkb, keymap, state);
    return true;
}

// Asks for the XKB events that change the keymap and the state, and for
// repeats without releases between them.
static void SelectEvents(const mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    const uint16_t events = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
                            XCB_XKB_EVENT_TYPE_STATE_NOTIFY;
    const uint16_t mapParts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS |
                              XCB_XKB_MAP_PART_MODIFIER_MAP | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
                              XCB_XKB_MAP_PART_KEY_ACTIONS | XCB_XKB_MAP_PART_VIRTUAL_MODS |
                              XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
    const uint16_t stateParts = XCB_XKB_STATE_PART_MODIFIER_BASE |
                                XCB_XKB_STATE_PART_MODIFIER_LATCH |
                                XCB_XKB_STATE_PART_MODIFIER_LOCK | XCB_XKB_STATE_PART_GROUP_BASE |
                                XCB_XKB_STATE_PART_GROUP_LATCH | XCB_XKB_STATE_PART_GROUP_LOCK;
    xcb_xkb_select_events_details_t details = {0};
    details.affectNewKeyboard = XCB_XKB_NKN_DETAIL_KEYCODES;
    details.newKeyboardDetails = XCB_XKB_NKN_DETAIL_KEYCODES;
    details.affectState = stateParts;
    details.stateDetails = stateParts;
    api->xkbSelectEvents(platform->connection, (xcb_xkb_device_spec_t)platform->keyboard.device,
                         events, 0, 0, mapParts, mapParts, &details);
    const uint32_t repeat = XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT;
    mwinReleaseSystemMemory(api->xkbPerClientFlagsReply(
        platform->connection,
        api->xkbPerClientFlags(platform->connection, XCB_XKB_ID_USE_CORE_KBD, repeat, repeat, 0, 0,
                               0),
        nullptr));
}

void mwinX11StartKeyboard(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    mwinX11Keyboard* keyboard = &platform->keyboard;
    if (api->xkbX11Library == nullptr || mwinLoadXkb(&platform->xkbApi) != mwin_success)
    {
        return;
    }
    uint8_t event = 0;
    if (api->xkbSetupExtension(platform->connection, XKB_X11_MIN_MAJOR_XKB_VERSION,
                               XKB_X11_MIN_MINOR_XKB_VERSION, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                               nullptr, nullptr, &event, nullptr) == 0 ||
        !mwinXkbStart(&keyboard->xkb, &platform->xkbApi))
    {
        return;
    }
    keyboard->device = api->xkbCoreDevice(platform->connection);
    if (keyboard->device < 0 || !ReadKeymap(platform))
    {
        mwinXkbStop(&keyboard->xkb);
        return;
    }
    keyboard->event = event;
    SelectEvents(platform);
}

void mwinX11StopKeyboard(mwinX11Platform* platform)
{
    mwinXkbStop(&platform->keyboard.xkb);
    mwinUnloadXkb(&platform->xkbApi);
}

static void PostLayoutChange(mwinX11Platform* platform)
{
    mwinEvent event = {0};
    event.type = mwin_eventKeyboardLayoutChanged;
    event.timeNs = mwinMonotonicNow();
    mwinPostGlobal(platform->context, &event);
}

// An XKB event: the keymap or the state changed.
static void OnXkb(mwinX11Platform* platform, const xcb_generic_event_t* event)
{
    mwinX11Keyboard* keyboard = &platform->keyboard;
    // Every XKB event shares one event code and has its kind next.
    uint8_t kind = ((const uint8_t*)event)[1];
    if (kind == XCB_XKB_NEW_KEYBOARD_NOTIFY || kind == XCB_XKB_MAP_NOTIFY)
    {
        if (ReadKeymap(platform))
        {
            PostLayoutChange(platform);
        }
        return;
    }
    if (kind != XCB_XKB_STATE_NOTIFY)
    {
        return;
    }
    const xcb_xkb_state_notify_event_t* state = (const xcb_xkb_state_notify_event_t*)event;
    if (mwinXkbUpdateState(&keyboard->xkb, state->baseMods, state->latchedMods, state->lockedMods,
                           (uint32_t)state->baseGroup, (uint32_t)state->latchedGroup,
                           state->lockedGroup))
    {
        PostLayoutChange(platform);
    }
}

static void OnKey(mwinX11Platform* platform, const xcb_key_press_event_t* event, bool pressed)
{
    mwinX11Keyboard* keyboard = &platform->keyboard;
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot < 0 || keyboard->xkb.state == nullptr || event->detail < XKB_OFFSET)
    {
        return;
    }
    uint32_t evdev = event->detail - XKB_OFFSET;
    uint8_t bit = (uint8_t)(1u << (event->detail & 7u));
    bool repeat = pressed && (keyboard->held[event->detail >> 3] & bit) != 0;
    keyboard->held[event->detail >> 3] = pressed
                                             ? keyboard->held[event->detail >> 3] | bit
                                             : keyboard->held[event->detail >> 3] & (uint8_t)~bit;
    mwinKeyCode code = mwinKeyCodeFromEvdev(evdev);
    mwinEvent record = {0};
    record.type = pressed ? mwin_eventKeyDown : mwin_eventKeyUp;
    record.timeNs = mwinMonotonicFromMilliseconds(event->time);
    record.data.key = (mwinKeyEvent){code, keyboard->xkb.modifiers,
                                     mwinXkbKeyOf(&keyboard->xkb, evdev, code), repeat};
    mwinPost(platform->context, (uint32_t)slot, &record);
    char text[MWIN_XKB_TEXT_BYTES];
    uint32_t length = pressed ? mwinXkbType(&keyboard->xkb, evdev, !repeat, text) : 0;
    if (length > 0)
    {
        mwinEvent typed = {0};
        typed.type = mwin_eventTextInput;
        typed.timeNs = record.timeNs;
        typed.data.text = (mwinTextEvent){text, length};
        mwinPost(platform->context, (uint32_t)slot, &typed);
    }
}

static void PostPointer(mwinX11Platform* platform, uint32_t slot, mwinEventType type,
                        mwinMouseButton button, uint64_t timeNs)
{
    const mwinX11Pointer* pointer = &platform->pointer;
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    event.data.pointer = (mwinPointerEvent){pointer->position, platform->keyboard.xkb.modifiers,
                                            pointer->buttons, button, pointer->clicks.clicks};
    mwinPost(platform->context, slot, &event);
}

static mwinPosition PositionOf(const mwinX11Platform* platform, int16_t x, int16_t y)
{
    return (mwinPosition){(float)x / platform->scale, (float)y / platform->scale};
}

// The wheel turned: X11's buttons 4 to 7, a detent each.
static void Turn(mwinX11Platform* platform, uint32_t slot, uint8_t button, uint64_t timeNs)
{
    mwinEvent event = {0};
    event.type = mwin_eventWheel;
    event.timeNs = timeNs;
    event.data.wheel = (mwinWheelEvent){button == 6 ? -1.0f : (button == 7 ? 1.0f : 0.0f),
                                        button == 4 ? 1.0f : (button == 5 ? -1.0f : 0.0f)};
    mwinPost(platform->context, slot, &event);
}

static void OnButton(mwinX11Platform* platform, const xcb_button_press_event_t* event, bool pressed)
{
    static const mwinMouseButton buttons[10] = {
        0, mwin_buttonLeft, mwin_buttonMiddle, mwin_buttonRight, 0, 0, 0,
        0, mwin_buttonBack, mwin_buttonForward};
    mwinX11Pointer* pointer = &platform->pointer;
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot < 0 || event->detail >= 10)
    {
        return;
    }
    uint64_t timeNs = mwinMonotonicFromMilliseconds(event->time);
    pointer->position = PositionOf(platform, event->event_x, event->event_y);
    if (event->detail >= 4 && event->detail <= 7)
    {
        if (pressed)
        {
            Turn(platform, (uint32_t)slot, event->detail, timeNs);
        }
        return;
    }
    mwinMouseButton button = buttons[event->detail];
    uint8_t bit = (uint8_t)(1u << (button - 1));
    if (pressed)
    {
        (void)mwinCountClick(&pointer->clicks, button, pointer->position, timeNs);
        pointer->buttons |= bit;
    }
    else
    {
        pointer->buttons &= (uint8_t)~bit;
    }
    PostPointer(platform, (uint32_t)slot, pressed ? mwin_eventButtonDown : mwin_eventButtonUp,
                button, timeNs);
}

static void OnMotion(mwinX11Platform* platform, const xcb_motion_notify_event_t* event)
{
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot >= 0)
    {
        platform->pointer.position = PositionOf(platform, event->event_x, event->event_y);
        PostPointer(platform, (uint32_t)slot, mwin_eventCursorMoved, 0,
                    mwinMonotonicFromMilliseconds(event->time));
    }
}

// The pointer crossed into or out of a window; crossings a grab makes
// are not the pointer's.
static void OnCrossing(mwinX11Platform* platform, const xcb_enter_notify_event_t* event,
                       bool entered)
{
    mwinX11Pointer* pointer = &platform->pointer;
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot < 0 || event->mode != XCB_NOTIFY_MODE_NORMAL)
    {
        return;
    }
    uint64_t timeNs = mwinMonotonicFromMilliseconds(event->time);
    pointer->position = PositionOf(platform, event->event_x, event->event_y);
    if (entered)
    {
        pointer->focus = slot;
        PostPointer(platform, (uint32_t)slot, mwin_eventCursorEntered, 0, timeNs);
        return;
    }
    PostPointer(platform, (uint32_t)slot, mwin_eventCursorLeft, 0, timeNs);
    pointer->focus = -1;
}

bool mwinX11HandleInputEvent(mwinX11Platform* platform, const xcb_generic_event_t* event)
{
    uint8_t type = event->response_type & 0x7F;
    if (platform->keyboard.event != 0 && type == platform->keyboard.event)
    {
        OnXkb(platform, event);
        return true;
    }
    switch (type)
    {
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
        OnKey(platform, (const xcb_key_press_event_t*)event, type == XCB_KEY_PRESS);
        return true;
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
        OnButton(platform, (const xcb_button_press_event_t*)event, type == XCB_BUTTON_PRESS);
        return true;
    case XCB_MOTION_NOTIFY:
        OnMotion(platform, (const xcb_motion_notify_event_t*)event);
        return true;
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
        OnCrossing(platform, (const xcb_enter_notify_event_t*)event, type == XCB_ENTER_NOTIFY);
        return true;
    default:
        return false;
    }
}

void mwinX11ForgetKeys(mwinX11Platform* platform)
{
    for (size_t i = 0; i < sizeof(platform->keyboard.held); i++)
    {
        platform->keyboard.held[i] = 0;
    }
    mwinXkbResetCompose(&platform->keyboard.xkb);
}

void mwinX11ForgetPointer(mwinX11Platform* platform, uint32_t slot)
{
    if (platform->pointer.focus == (int32_t)slot)
    {
        platform->pointer.focus = -1;
        platform->pointer.buttons = 0;
    }
}

mwinKey mwinX11MapKeyCode(const mwinX11Platform* platform, mwinKeyCode code)
{
    return mwinXkbMapKeyCode(&platform->keyboard.xkb, code);
}

mwinResult mwinX11KeyboardLayout(const mwinX11Platform* platform, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    return mwinXkbLayoutName(&platform->keyboard.xkb, buffer, capacity, lengthOut);
}
