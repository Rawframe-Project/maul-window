// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 gamepads through XInput.

#include "win32_pad.h"

#include <string.h>

#define SEARCH_NS 500000000u

// XInput's buttons in mwinGamepadButton order; the guide button is not
// in its public functions.
static const WORD s_buttons[MWIN_GAMEPAD_BUTTONS] = {
    XINPUT_GAMEPAD_DPAD_UP,
    XINPUT_GAMEPAD_DPAD_DOWN,
    XINPUT_GAMEPAD_DPAD_LEFT,
    XINPUT_GAMEPAD_DPAD_RIGHT,
    XINPUT_GAMEPAD_A,
    XINPUT_GAMEPAD_B,
    XINPUT_GAMEPAD_X,
    XINPUT_GAMEPAD_Y,
    XINPUT_GAMEPAD_LEFT_SHOULDER,
    XINPUT_GAMEPAD_RIGHT_SHOULDER,
    XINPUT_GAMEPAD_LEFT_THUMB,
    XINPUT_GAMEPAD_RIGHT_THUMB,
    XINPUT_GAMEPAD_START,
    XINPUT_GAMEPAD_BACK,
    0,
};

// A function of the library, its bytes copied: a FARPROC is no function
// of the right type to cast.
static void Load(HMODULE library, const char* name, void* function, size_t size)
{
    FARPROC found = GetProcAddress(library, name);
    memcpy(function, (const void*)&found, size);
}

void mwinWin32PadsStart(mwinWin32Pads* pads, mwinContext* context)
{
    *pads = (mwinWin32Pads){.context = context};
    static const LPCWSTR libraries[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (size_t i = 0; i < 3 && pads->api.library == nullptr; i++)
    {
        pads->api.library = LoadLibraryW(libraries[i]);
    }
    if (pads->api.library == nullptr)
    {
        return;
    }
    Load(pads->api.library, "XInputGetState", (void*)&pads->api.getState,
         sizeof(pads->api.getState));
    Load(pads->api.library, "XInputSetState", (void*)&pads->api.setState,
         sizeof(pads->api.setState));
    Load(pads->api.library, "XInputGetBatteryInformation", (void*)&pads->api.getBattery,
         sizeof(pads->api.getBattery));
}

void mwinWin32PadsStop(mwinWin32Pads* pads)
{
    for (DWORD i = 0; i < MWIN_WIN32_PADS && pads->api.setState != nullptr; i++)
    {
        if (pads->pads[i].connected && pads->pads[i].rumbleEndsNs != 0)
        {
            XINPUT_VIBRATION still = {0, 0};
            (void)pads->api.setState(i, &still);
        }
    }
    if (pads->api.library != nullptr)
    {
        FreeLibrary(pads->api.library);
    }
    *pads = (mwinWin32Pads){0};
}

// The charge of a wireless pad's battery in percent, or -1.
static int8_t BatteryOf(const mwinWin32Pads* pads, DWORD user)
{
    XINPUT_BATTERY_INFORMATION battery = {0};
    if (pads->api.getBattery == nullptr ||
        pads->api.getBattery(user, BATTERY_DEVTYPE_GAMEPAD, &battery) != ERROR_SUCCESS ||
        battery.BatteryType == BATTERY_TYPE_WIRED ||
        battery.BatteryType == BATTERY_TYPE_DISCONNECTED ||
        battery.BatteryType == BATTERY_TYPE_UNKNOWN)
    {
        return -1;
    }
    static const int8_t levels[] = {0, 33, 66, 100};
    return levels[battery.BatteryLevel & 3u];
}

static mwinGamepadInfo InfoOf(int8_t battery)
{
    static const char name[] = "XInput Controller";
    mwinGamepadInfo info = {.mapped = true, .capabilities = mwin_padRumble, .battery = battery};
    info.nameLength = sizeof(name) - 1;
    memcpy(info.name, name, info.nameLength);
    return info;
}

static float Stick(SHORT value)
{
    return value < 0 ? (float)value / 32768.0f : (float)value / 32767.0f;
}

// Posts a pad's state; sticks' y turned so down is positive.
static void Post(mwinWin32Pads* pads, const mwinWin32Pad* pad, const XINPUT_GAMEPAD* state,
                 uint64_t timeNs)
{
    mwinContext* context = pads->context;
    for (uint8_t i = 0; i < MWIN_GAMEPAD_BUTTONS; i++)
    {
        bool down = s_buttons[i] != 0 && (state->wButtons & s_buttons[i]) != 0;
        mwinPostGamepadButton(context, pad->slot, i, down, timeNs);
    }
    const float axes[MWIN_GAMEPAD_AXES] = {
        Stick(state->sThumbLX),
        -Stick(state->sThumbLY),
        Stick(state->sThumbRX),
        -Stick(state->sThumbRY),
        (float)state->bLeftTrigger / 255.0f,
        (float)state->bRightTrigger / 255.0f,
    };
    for (uint8_t i = 0; i < MWIN_GAMEPAD_AXES; i++)
    {
        mwinPostGamepadAxis(context, pad->slot, i, axes[i], timeNs);
    }
}

// Reads a player slot: a pad connected, still there, or gone.
static void Read(mwinWin32Pads* pads, DWORD user, uint64_t nowNs)
{
    mwinWin32Pad* pad = &pads->pads[user];
    XINPUT_STATE state = {0};
    if (pads->api.getState(user, &state) != ERROR_SUCCESS)
    {
        if (pad->connected)
        {
            mwinRemoveGamepad(pads->context, pad->slot, nowNs);
            *pad = (mwinWin32Pad){0};
        }
        return;
    }
    if (!pad->connected)
    {
        mwinGamepadInfo info = InfoOf(BatteryOf(pads, user));
        int32_t slot = mwinAddGamepad(pads->context, &info, nowNs);
        if (slot < 0)
        {
            return;
        }
        *pad = (mwinWin32Pad){.connected = true,
                              .slot = (uint32_t)slot,
                              .packet = state.dwPacketNumber - 1,
                              .battery = info.battery};
    }
    if (state.dwPacketNumber != pad->packet)
    {
        pad->packet = state.dwPacketNumber;
        Post(pads, pad, &state.Gamepad, nowNs);
    }
}

void mwinWin32PadsPump(mwinWin32Pads* pads, uint64_t nowNs)
{
    if (pads->api.getState == nullptr)
    {
        return;
    }
    bool search = nowNs - pads->searchedNs >= SEARCH_NS || pads->searchedNs == 0;
    pads->searchedNs = search ? nowNs : pads->searchedNs;
    for (DWORD user = 0; user < MWIN_WIN32_PADS; user++)
    {
        mwinWin32Pad* pad = &pads->pads[user];
        if (pad->connected || search)
        {
            Read(pads, user, nowNs);
        }
        if (pad->connected && pad->rumbleEndsNs != 0 && nowNs >= pad->rumbleEndsNs)
        {
            XINPUT_VIBRATION still = {0, 0};
            (void)pads->api.setState(user, &still);
            pad->rumbleEndsNs = 0;
        }
    }
}

mwinResult mwinWin32PadsRumble(mwinWin32Pads* pads, uint32_t slot, float low, float high,
                               uint32_t durationMs, uint64_t nowNs)
{
    for (DWORD user = 0; user < MWIN_WIN32_PADS && pads->api.setState != nullptr; user++)
    {
        mwinWin32Pad* pad = &pads->pads[user];
        if (!pad->connected || pad->slot != slot)
        {
            continue;
        }
        XINPUT_VIBRATION vibration = {(WORD)(low * 65535.0f), (WORD)(high * 65535.0f)};
        if (durationMs == 0)
        {
            vibration = (XINPUT_VIBRATION){0, 0};
        }
        if (pads->api.setState(user, &vibration) != ERROR_SUCCESS)
        {
            return mwin_errorPlatform;
        }
        pad->rumbleEndsNs = durationMs > 0 ? nowNs + (uint64_t)durationMs * 1000000u : 0;
        return mwin_success;
    }
    return mwin_errorPlatform;
}
