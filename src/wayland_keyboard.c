// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland keyboard.

#include "wayland_keyboard.h"

#include "evdev.h"

#include "maul-unicode/encoding.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// The most repeats one pump posts after a stall; the rest are dropped.
#define MAX_REPEATS_PER_PUMP 8

// The longest text one key types.
#define KEY_TEXT_BYTES 64

// Evdev codes are xkb key codes less 8.
#define XKB_OFFSET 8

// The locale compose sequences follow, from the environment as the C
// library would read it.
static const char* Locale(void)
{
    static const char* const names[] = {"LC_ALL", "LC_CTYPE", "LANG"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        const char* value = getenv(names[i]);
        if (value != nullptr && value[0] != '\0')
        {
            return value;
        }
    }
    return "C";
}

static void PostGlobal(mwinWaylandPlatform* platform, mwinEventType type)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = mwinWaylandNow();
    mwinPostGlobal(platform->context, &event);
}

static void DropKeymap(mwinWaylandPlatform* platform)
{
    const mwinXkbApi* xkb = &platform->xkb;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->state != nullptr)
    {
        xkb->stateUnref(keyboard->state);
        keyboard->state = nullptr;
    }
    if (keyboard->keymap != nullptr)
    {
        xkb->keymapUnref(keyboard->keymap);
        keyboard->keymap = nullptr;
    }
}

// What a key types with no modifier in the current layout: its code
// point, or MWIN_KEY_NAMED with its code for a key that types nothing.
static mwinKey MapKey(const mwinWaylandPlatform* platform, uint32_t evdev, mwinKeyCode code)
{
    const mwinXkbApi* xkb = &platform->xkb;
    const mwinWaylandKeyboard* keyboard = &platform->keyboard;
    const xkb_keysym_t* symbols = nullptr;
    if (keyboard->keymap != nullptr &&
        xkb->keymapKeyGetSymsByLevel(keyboard->keymap, evdev + XKB_OFFSET, keyboard->layout, 0,
                                     &symbols) == 1)
    {
        uint32_t character = xkb->keysymToUtf32(symbols[0]);
        if (character >= 0x20u && character != 0x7Fu)
        {
            return character;
        }
    }
    return MWIN_KEY_NAMED | code;
}

static void OnKeymap(void* data, struct wl_keyboard* object, uint32_t format, int32_t fd,
                     uint32_t size)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    const mwinXkbApi* xkb = &platform->xkb;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || keyboard->context == nullptr || size == 0)
    {
        close(fd);
        return;
    }
    void* text = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (text == MAP_FAILED)
    {
        return;
    }
    // The text ends in a NUL, which the size counts.
    struct xkb_keymap* keymap =
        xkb->keymapNewFromBuffer(keyboard->context, text, strnlen(text, size),
                                 XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(text, size);
    struct xkb_state* state = keymap != nullptr ? xkb->stateNew(keymap) : nullptr;
    if (state == nullptr)
    {
        if (keymap != nullptr)
        {
            xkb->keymapUnref(keymap);
        }
        return;
    }
    DropKeymap(platform);
    keyboard->keymap = keymap;
    keyboard->state = state;
    keyboard->layout = 0;
    keyboard->repeatKey = 0;
    PostGlobal(platform, mwin_eventKeyboardLayoutChanged);
}

static void OnEnter(void* data, struct wl_keyboard* object, uint32_t serial,
                    struct wl_surface* surface, struct wl_array* keys)
{
    (void)object;
    (void)serial;
    (void)keys;
    mwinWaylandPlatform* platform = data;
    // Keys already held when focus comes are not reported as pressed.
    platform->keyboard.focus = mwinWaylandSlotOf(platform, surface);
    platform->keyboard.held = 0;
    platform->keyboard.repeatKey = 0;
}

static void OnLeave(void* data, struct wl_keyboard* object, uint32_t serial,
                    struct wl_surface* surface)
{
    (void)object;
    (void)serial;
    (void)surface;
    mwinWaylandPlatform* platform = data;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->focus >= 0 && keyboard->held > 0)
    {
        // Their releases will not come.
        mwinEvent reset = {0};
        reset.type = mwin_eventInputStateReset;
        reset.timeNs = mwinWaylandNow();
        mwinPost(platform->context, (uint32_t)keyboard->focus, &reset);
    }
    keyboard->focus = -1;
    keyboard->held = 0;
    keyboard->repeatKey = 0;
    if (keyboard->compose != nullptr)
    {
        platform->xkb.composeStateReset(keyboard->compose);
    }
}

static void PostText(mwinWaylandPlatform* platform, const char* text, int length, uint64_t timeNs)
{
    // Control characters (Enter, Tab, Backspace, Escape, and what
    // Control makes of letters) are keys, not text.
    if (length <= 0 || (unsigned char)text[0] < 0x20u || text[0] == 0x7F)
    {
        return;
    }
    mwinEvent event = {0};
    event.type = mwin_eventTextInput;
    event.timeNs = timeNs;
    event.data.text = (mwinTextEvent){text, (uint32_t)length};
    mwinPost(platform->context, (uint32_t)platform->keyboard.focus, &event);
}

// The text a key typed, through a compose sequence when one is running
// or starts with it.
static void TypeText(mwinWaylandPlatform* platform, uint32_t evdev, uint64_t timeNs)
{
    const mwinXkbApi* xkb = &platform->xkb;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    char text[KEY_TEXT_BYTES];
    xkb_keycode_t key = evdev + XKB_OFFSET;
    struct xkb_compose_state* compose = keyboard->compose;
    if (compose != nullptr &&
        xkb->composeStateFeed(compose, xkb->stateKeyGetOneSym(keyboard->state, key)) ==
            XKB_COMPOSE_FEED_ACCEPTED)
    {
        switch (xkb->composeStateGetStatus(compose))
        {
        case XKB_COMPOSE_COMPOSING:
            return;
        case XKB_COMPOSE_COMPOSED:
            PostText(platform, text, xkb->composeStateGetUtf8(compose, text, sizeof(text)), timeNs);
            xkb->composeStateReset(compose);
            return;
        case XKB_COMPOSE_CANCELLED:
            xkb->composeStateReset(compose);
            return;
        default:
            break;
        }
    }
    int length = xkb->stateKeyGetUtf8(keyboard->state, key, text, sizeof(text));
    PostText(platform, text, length < (int)sizeof(text) ? length : 0, timeNs);
}

static void PostKey(mwinWaylandPlatform* platform, mwinEventType type, uint32_t evdev, bool repeat,
                    uint64_t timeNs)
{
    mwinKeyCode code = mwinKeyCodeFromEvdev(evdev);
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    event.data.key =
        (mwinKeyEvent){code, platform->keyboard.modifiers, MapKey(platform, evdev, code), repeat};
    mwinPost(platform->context, (uint32_t)platform->keyboard.focus, &event);
}

static void OnKey(void* data, struct wl_keyboard* object, uint32_t serial, uint32_t time,
                  uint32_t evdev, uint32_t state)
{
    (void)object;
    (void)serial;
    mwinWaylandPlatform* platform = data;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->focus < 0 || keyboard->state == nullptr)
    {
        return;
    }
    uint64_t timeNs = mwinWaylandTime(time);
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        keyboard->held -= keyboard->held > 0 ? 1 : 0;
        keyboard->repeatKey = evdev == keyboard->repeatKey ? 0 : keyboard->repeatKey;
        PostKey(platform, mwin_eventKeyUp, evdev, false, timeNs);
        return;
    }
    keyboard->held += 1;
    PostKey(platform, mwin_eventKeyDown, evdev, false, timeNs);
    TypeText(platform, evdev, timeNs);
    if (keyboard->repeatRate > 0 &&
        platform->xkb.keymapKeyRepeats(keyboard->keymap, evdev + XKB_OFFSET) != 0)
    {
        keyboard->repeatKey = evdev;
        keyboard->repeatNextNs = timeNs + (uint64_t)keyboard->repeatDelayMs * 1000000u;
    }
}

// The modifiers an xkb state has in effect.
static mwinModifiers ModifiersOf(const mwinXkbApi* xkb, struct xkb_state* state)
{
    static const struct
    {
        const char* name;
        mwinModifiers modifier;
    } table[] = {
        {XKB_MOD_NAME_SHIFT, mwin_modShift},   {XKB_MOD_NAME_CTRL, mwin_modControl},
        {XKB_MOD_NAME_ALT, mwin_modAlt},       {XKB_MOD_NAME_LOGO, mwin_modMeta},
        {XKB_MOD_NAME_CAPS, mwin_modCapsLock}, {XKB_MOD_NAME_NUM, mwin_modNumLock},
    };
    mwinModifiers modifiers = 0;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        if (xkb->stateModNameIsActive(state, table[i].name, XKB_STATE_MODS_EFFECTIVE) > 0)
        {
            modifiers |= table[i].modifier;
        }
    }
    return modifiers;
}

static void OnModifiers(void* data, struct wl_keyboard* object, uint32_t serial, uint32_t depressed,
                        uint32_t latched, uint32_t locked, uint32_t group)
{
    (void)object;
    (void)serial;
    mwinWaylandPlatform* platform = data;
    const mwinXkbApi* xkb = &platform->xkb;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->state == nullptr)
    {
        return;
    }
    xkb->stateUpdateMask(keyboard->state, depressed, latched, locked, 0, 0, group);
    keyboard->modifiers = ModifiersOf(xkb, keyboard->state);
    uint32_t layout = xkb->stateSerializeLayout(keyboard->state, XKB_STATE_LAYOUT_EFFECTIVE);
    if (layout != keyboard->layout)
    {
        keyboard->layout = layout;
        PostGlobal(platform, mwin_eventKeyboardLayoutChanged);
    }
}

static void OnRepeatInfo(void* data, struct wl_keyboard* object, int32_t rate, int32_t delay)
{
    (void)object;
    mwinWaylandKeyboard* keyboard = &((mwinWaylandPlatform*)data)->keyboard;
    keyboard->repeatRate = rate > 0 ? rate : 0;
    keyboard->repeatDelayMs = delay > 0 ? delay : 0;
}

static const struct wl_keyboard_listener s_keyboardListener = {
    OnKeymap, OnEnter, OnLeave, OnKey, OnModifiers, OnRepeatInfo,
};

void mwinWaylandAddKeyboard(mwinWaylandPlatform* platform)
{
    const mwinXkbApi* xkb = &platform->xkb;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (xkb->library == nullptr)
    {
        return;
    }
    *keyboard = (mwinWaylandKeyboard){.focus = -1, .repeatRate = 25, .repeatDelayMs = 600};
    keyboard->context = xkb->contextNew(XKB_CONTEXT_NO_FLAGS);
    if (keyboard->context == nullptr)
    {
        return;
    }
    // Without a compose table for the locale, keys type what they type.
    keyboard->composeTable =
        xkb->composeTableNewFromLocale(keyboard->context, Locale(), XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (keyboard->composeTable != nullptr)
    {
        keyboard->compose =
            xkb->composeStateNew(keyboard->composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
    }
    keyboard->keyboard = mwinWlRequest(&platform->api, platform->seat, WL_SEAT_GET_KEYBOARD,
                                       &wl_keyboard_interface, 0);
    mwinWlListen(&platform->api, keyboard->keyboard, &s_keyboardListener, platform);
}

void mwinWaylandRemoveKeyboard(mwinWaylandPlatform* platform)
{
    const mwinWaylandApi* api = &platform->api;
    const mwinXkbApi* xkb = &platform->xkb;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->keyboard != nullptr)
    {
        if (mwinWlVersion(api, keyboard->keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
        {
            (void)mwinWlRequest(api, keyboard->keyboard, WL_KEYBOARD_RELEASE, nullptr,
                                WL_MARSHAL_FLAG_DESTROY);
        }
        else
        {
            api->proxyDestroy((struct wl_proxy*)keyboard->keyboard);
        }
    }
    DropKeymap(platform);
    if (keyboard->compose != nullptr)
    {
        xkb->composeStateUnref(keyboard->compose);
    }
    if (keyboard->composeTable != nullptr)
    {
        xkb->composeTableUnref(keyboard->composeTable);
    }
    if (keyboard->context != nullptr)
    {
        xkb->contextUnref(keyboard->context);
    }
    *keyboard = (mwinWaylandKeyboard){.focus = -1};
}

void mwinWaylandRepeatKeys(mwinWaylandPlatform* platform)
{
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->repeatKey == 0 || keyboard->focus < 0 || keyboard->repeatRate <= 0)
    {
        return;
    }
    uint64_t now = mwinWaylandNow();
    uint64_t interval = 1000000000u / (uint64_t)keyboard->repeatRate;
    for (int i = 0; i < MAX_REPEATS_PER_PUMP && keyboard->repeatNextNs <= now; i++)
    {
        uint64_t timeNs = keyboard->repeatNextNs;
        PostKey(platform, mwin_eventKeyDown, keyboard->repeatKey, true, timeNs);
        char text[KEY_TEXT_BYTES];
        int length = platform->xkb.stateKeyGetUtf8(
            keyboard->state, keyboard->repeatKey + XKB_OFFSET, text, sizeof(text));
        PostText(platform, text, length < (int)sizeof(text) ? length : 0, timeNs);
        keyboard->repeatNextNs += interval;
    }
    if (keyboard->repeatNextNs <= now)
    {
        keyboard->repeatNextNs = now + interval;
    }
}

void mwinWaylandForgetKeyboardFocus(mwinWaylandPlatform* platform, uint32_t slot)
{
    if (platform->keyboard.focus == (int32_t)slot)
    {
        platform->keyboard.focus = -1;
        platform->keyboard.repeatKey = 0;
    }
}

mwinKey mwinWaylandMapKeyCode(const mwinWaylandPlatform* platform, mwinKeyCode code)
{
    uint32_t evdev = mwinEvdevFromKeyCode(code);
    return evdev != 0 ? MapKey(platform, evdev, code) : MWIN_KEY_NAMED | code;
}

mwinResult mwinWaylandKeyboardLayout(const mwinWaylandPlatform* platform, char* buffer,
                                     size_t capacity, size_t* lengthOut)
{
    const mwinWaylandKeyboard* keyboard = &platform->keyboard;
    const char* name = keyboard->keymap != nullptr
                           ? platform->xkb.keymapLayoutGetName(keyboard->keymap, keyboard->layout)
                           : nullptr;
    size_t length = name != nullptr ? strlen(name) : 0;
    // A name that is not UTF-8 is no name.
    if (length > 0 && muniValidateUtf8(name, length).status != muni_success)
    {
        length = 0;
    }
    if (capacity > 0 && length > 0)
    {
        memcpy(buffer, name, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}
