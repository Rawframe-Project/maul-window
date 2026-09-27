// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 gamepads (src/win32_pad.c) against a stand-in for XInput,
// driven on a context of the test backend: a pad found when the free
// player slots are looked at, and not sooner than every half second;
// its facts and battery; its buttons, sticks with y turned down, and
// triggers, read only when XInput's packet number moves; rumble with
// both motors, stopped when its time runs out; and a disconnect.

#include "test_program.h"
#include "win32_pad.h"

#include <string.h>

// The stand-in's player slots, and the motors' last speeds.
typedef struct Fake
{
    bool connected[MWIN_WIN32_PADS];
    XINPUT_STATE states[MWIN_WIN32_PADS];
    XINPUT_VIBRATION vibration;
    int vibrations;
    int reads;
} Fake;

static Fake s_fake;
static mwinWin32Pads s_pads;

static DWORD WINAPI GetState(DWORD user, XINPUT_STATE* state)
{
    s_fake.reads += 1;
    if (!s_fake.connected[user])
    {
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    *state = s_fake.states[user];
    return ERROR_SUCCESS;
}

static DWORD WINAPI SetState(DWORD user, XINPUT_VIBRATION* vibration)
{
    (void)user;
    s_fake.vibration = *vibration;
    s_fake.vibrations += 1;
    return ERROR_SUCCESS;
}

static DWORD WINAPI GetBattery(DWORD user, BYTE type, XINPUT_BATTERY_INFORMATION* battery)
{
    (void)user;
    (void)type;
    *battery = (XINPUT_BATTERY_INFORMATION){BATTERY_TYPE_NIMH, BATTERY_LEVEL_MEDIUM};
    return ERROR_SUCCESS;
}

#define MS 1000000u

static void Connect(Program* program, mwinContext* context)
{
    (void)program;
    s_pads = (mwinWin32Pads){.context = context};
    s_pads.api = (mwinXInputApi){nullptr, GetState, SetState, GetBattery};
    s_fake.connected[1] = true;
    s_fake.states[1].dwPacketNumber = 1;
    mwinWin32PadsPump(&s_pads, 1000 * MS);
}

static void CheckConnected(Program* program, mwinContext* context)
{
    CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventGamepadAdded,
          "a pad found");
    mwinGamepadInfo info;
    mwinGamepadId pad = program->events[0].data.gamepad;
    CHECK(mwinGetGamepadInfo(context, pad, &info) == mwin_success && info.mapped &&
              info.battery == 66 && (info.capabilities & mwin_padRumble) != 0 &&
              info.nameLength == 17,
          "its facts and battery");
    XINPUT_GAMEPAD* gamepad = &s_fake.states[1].Gamepad;
    s_fake.states[1].dwPacketNumber = 2;
    gamepad->wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_UP;
    gamepad->sThumbLX = 32767;
    gamepad->sThumbLY = 32767;
    gamepad->bRightTrigger = 255;
    mwinWin32PadsPump(&s_pads, 1010 * MS);
}

static bool Has(const Program* program, mwinEventType type, uint8_t control, float value)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        bool button = event->type == type && type == mwin_eventGamepadButtonDown &&
                      event->data.gamepadButton.button == control;
        bool axis = event->type == type && type == mwin_eventGamepadAxisMoved &&
                    event->data.gamepadAxis.axis == control &&
                    event->data.gamepadAxis.value == value;
        if (button || axis)
        {
            return true;
        }
    }
    return false;
}

static void CheckPressed(Program* program)
{
    CHECK(Has(program, mwin_eventGamepadButtonDown, mwin_padFaceSouth, 0.0f) &&
              Has(program, mwin_eventGamepadButtonDown, mwin_padDpadUp, 0.0f),
          "A south, and the d-pad");
    CHECK(Has(program, mwin_eventGamepadAxisMoved, mwin_padStickLeftX, 1.0f) &&
              Has(program, mwin_eventGamepadAxisMoved, mwin_padStickLeftY, -1.0f) &&
              Has(program, mwin_eventGamepadAxisMoved, mwin_padTriggerRight, 1.0f),
          "a stick right and up (y down positive), a trigger in");
    // The same packet: nothing is read again.
    s_fake.states[1].Gamepad.wButtons = 0;
    mwinWin32PadsPump(&s_pads, 1020 * MS);
}

static void CheckRumble(Program* program)
{
    CHECK(program->eventCount == 0, "an unchanged packet posts nothing");
    uint32_t slot = s_pads.pads[1].slot;
    CHECK(mwinWin32PadsRumble(&s_pads, slot, 1.0f, 0.5f, 100, 1030 * MS) == mwin_success &&
              s_fake.vibration.wLeftMotorSpeed == 65535 &&
              s_fake.vibration.wRightMotorSpeed == 32767,
          "rumble, the heavy motor left");
    mwinWin32PadsPump(&s_pads, 1080 * MS);
    CHECK(s_fake.vibration.wLeftMotorSpeed == 65535, "still running before its time");
    mwinWin32PadsPump(&s_pads, 1140 * MS);
    CHECK(s_fake.vibration.wLeftMotorSpeed == 0 && s_fake.vibration.wRightMotorSpeed == 0,
          "stopped when its time runs out");
}

static void CheckSearch(Program* program)
{
    (void)program;
    s_fake.connected[3] = true;
    int reads = s_fake.reads;
    mwinWin32PadsPump(&s_pads, 1150 * MS);
    CHECK(s_fake.reads == reads + 1 && !s_pads.pads[3].connected,
          "free slots are not asked about each pump");
    mwinWin32PadsPump(&s_pads, 1600 * MS);
    CHECK(s_pads.pads[3].connected, "but every half second");
    s_fake.connected[1] = false;
    mwinWin32PadsPump(&s_pads, 1610 * MS);
}

static void Step(Program* program, mwinContext* context, int step)
{
    if (step > 0)
    {
        Drain(program, context);
    }
    switch (step)
    {
    case 0:
        Connect(program, context);
        break;
    case 1:
        CheckConnected(program, context);
        break;
    case 2:
        CheckPressed(program);
        break;
    case 3:
        CheckRumble(program);
        CheckSearch(program);
        break;
    default:
    {
        const mwinEvent* last = &program->events[program->eventCount - 1];
        CHECK(program->eventCount >= 2 && last->type == mwin_eventGamepadRemoved,
              "a new pad, and a disconnect");
        program->done = true;
        break;
    }
    }
}

int main(void)
{
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
