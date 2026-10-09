// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An Android gamepad's controls turned into records (android_pad_map.h),
// on any platform, posted for gamepads of the test backend: a known
// gamepad's layout (the right stick on Z and RZ, the triggers on
// LTRIGGER and RTRIGGER, a hat), a Linux gamepad's generic one (the
// right stick on RX and RY, the triggers on Z and RZ from -1..1 to
// 0..1, at rest until Android's first 0 moves), BRAKE and GAS as triggers, triggers from the L2 and
// R2 keys, the buttons by place (X west, Y north, Back as select, Menu as start), and a raw
// joystick's buttons numbered among the keys it has and its axes with Android's values.

#include "android_pad_map.h"
#include "test_program.h"

// Android's axes and keys the layouts are described with.
enum
{
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 11,
    AXIS_RX = 12,
    AXIS_RY = 13,
    AXIS_RZ = 14,
    AXIS_HAT_X = 15,
    AXIS_HAT_Y = 16,
    AXIS_LTRIGGER = 17,
    AXIS_RTRIGGER = 18,
    AXIS_GAS = 22,
    AXIS_BRAKE = 23,
    KEY_BACK = 4,
    KEY_MENU = 82,
    KEY_BUTTON_A = 96,
    KEY_BUTTON_X = 99,
    KEY_BUTTON_Y = 100,
    KEY_BUTTON_L2 = 104,
    KEY_BUTTON_1 = 188,
};

static uint64_t s_timeNs = 1000000000u;

// The bit of a key in a layout.
static uint64_t KeyBit(int32_t keyCode)
{
    for (uint32_t i = 0; i < MWIN_ANDROID_PAD_KEYS; i++)
    {
        if (mwinAndroidPadKeys[i] == keyCode)
        {
            return 1ull << i;
        }
    }
    return 0;
}

// A layout of keys and axes, each axis -1..1 but triggers 0..1, as
// Android normalizes them; its gamepad added to the core.
static mwinAndroidPadLayout Layout(mwinContext* context, uint64_t keys, const int32_t* axes,
                                   uint32_t count, uint32_t* slotOut)
{
    mwinAndroidPadLayout layout = {.keys = keys, .axisCount = count};
    for (uint32_t i = 0; i < count; i++)
    {
        bool trigger = axes[i] == AXIS_LTRIGGER || axes[i] == AXIS_RTRIGGER ||
                       axes[i] == AXIS_GAS || axes[i] == AXIS_BRAKE;
        layout.axes[i] = axes[i];
        layout.min[i] = trigger ? 0.0f : -1.0f;
        layout.max[i] = 1.0f;
    }
    mwinGamepadInfo info = {.battery = -1};
    mwinAndroidPadLayoutOf(&layout, &info);
    *slotOut = (uint32_t)mwinAddGamepad(context, &info, s_timeNs);
    return layout;
}

static mwinGamepadState StateOf(mwinContext* context, uint32_t slot)
{
    mwinGamepadState state = {0};
    (void)mwinGetGamepadState(context, mwinGamepadIdOf(context, slot), &state);
    return state;
}

static void CheckKnown(Program* program, mwinContext* context)
{
    static const int32_t axes[] = {AXIS_X,     AXIS_Y,     AXIS_Z,        AXIS_RZ,
                                   AXIS_HAT_X, AXIS_HAT_Y, AXIS_LTRIGGER, AXIS_RTRIGGER};
    uint32_t slot = 0;
    uint64_t keys = KeyBit(KEY_BUTTON_A) | KeyBit(KEY_BUTTON_X) | KeyBit(KEY_BUTTON_Y);
    mwinAndroidPadLayout layout = Layout(context, keys, axes, 8, &slot);
    CHECK(layout.mapped && !layout.keyTriggers, "a known gamepad is mapped, analog triggers");
    const float values[] = {0.5f, -1.0f, -0.25f, 1.0f, -1.0f, 1.0f, 1.0f, 0.5f};
    mwinAndroidPadAxes(context, slot, &layout, values, s_timeNs);
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_X, true, s_timeNs);
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_Y, true, s_timeNs);
    mwinAndroidPadKey(context, slot, &layout, KEY_BACK, true, s_timeNs);
    mwinAndroidPadKey(context, slot, &layout, KEY_MENU, true, s_timeNs);
    mwinGamepadState state = StateOf(context, slot);
    CHECK(state.axes[mwin_padStickLeftX] == 0.5f && state.axes[mwin_padStickLeftY] == -1.0f,
          "the left stick on X and Y");
    CHECK(state.axes[mwin_padStickRightX] == -0.25f && state.axes[mwin_padStickRightY] == 1.0f,
          "the right stick on Z and RZ");
    CHECK(state.axes[mwin_padTriggerLeft] == 1.0f && state.axes[mwin_padTriggerRight] == 0.5f,
          "the triggers on LTRIGGER and RTRIGGER");
    uint32_t pressed = 1u << mwin_padDpadLeft | 1u << mwin_padDpadDown | 1u << mwin_padFaceWest |
                       1u << mwin_padFaceNorth | 1u << mwin_padSelect | 1u << mwin_padStart;
    CHECK(state.buttons == pressed,
          "the hat as the dpad; X west, Y north, Back as select, Menu as start");
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_L2, true, s_timeNs);
    CHECK(StateOf(context, slot).axes[mwin_padTriggerLeft] == 1.0f &&
              StateOf(context, slot).buttons == pressed,
          "L2 beside an analog trigger left alone");
    Drain(program, context);
}

static void CheckGeneric(Program* program, mwinContext* context)
{
    static const int32_t axes[] = {AXIS_X, AXIS_Y, AXIS_Z, AXIS_RX, AXIS_RY, AXIS_RZ};
    uint32_t slot = 0;
    mwinAndroidPadLayout layout = Layout(context, KeyBit(KEY_BUTTON_A), axes, 6, &slot);
    const float values[] = {0.0f, 0.0f, -1.0f, 0.75f, -0.5f, 0.0f};
    mwinAndroidPadAxes(context, slot, &layout, values, s_timeNs);
    mwinGamepadState state = StateOf(context, slot);
    CHECK(state.axes[mwin_padStickRightX] == 0.75f && state.axes[mwin_padStickRightY] == -0.5f,
          "a generic gamepad's right stick on RX and RY");
    CHECK(state.axes[mwin_padTriggerLeft] == 0.0f && state.axes[mwin_padTriggerRight] == 0.0f,
          "its triggers on Z and RZ, from -1..1 to 0..1; RZ at Android's first 0 at rest");
    const float pressed[] = {0.0f, 0.0f, -1.0f, 0.75f, -0.5f, 0.5f};
    mwinAndroidPadAxes(context, slot, &layout, pressed, s_timeNs);
    CHECK(StateOf(context, slot).axes[mwin_padTriggerRight] == 0.75f, "RZ moved: 0.5 is 0.75");
    mwinAndroidPadAxes(context, slot, &layout, values, s_timeNs);
    CHECK(StateOf(context, slot).axes[mwin_padTriggerRight] == 0.5f,
          "once moved, 0 is the middle of its travel");
    Drain(program, context);
}

static void CheckTriggers(Program* program, mwinContext* context)
{
    static const int32_t pedals[] = {AXIS_X, AXIS_Y, AXIS_Z, AXIS_RZ, AXIS_GAS, AXIS_BRAKE};
    uint32_t slot = 0;
    mwinAndroidPadLayout layout = Layout(context, KeyBit(KEY_BUTTON_A), pedals, 6, &slot);
    const float values[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 1.0f};
    mwinAndroidPadAxes(context, slot, &layout, values, s_timeNs);
    mwinGamepadState state = StateOf(context, slot);
    CHECK(state.axes[mwin_padTriggerLeft] == 1.0f && state.axes[mwin_padTriggerRight] == 0.25f,
          "BRAKE and GAS as the left and right triggers");
    static const int32_t sticks[] = {AXIS_X, AXIS_Y, AXIS_Z, AXIS_RZ};
    layout = Layout(context, KeyBit(KEY_BUTTON_A) | KeyBit(KEY_BUTTON_L2), sticks, 4, &slot);
    CHECK(layout.keyTriggers, "no trigger axes: the L2 and R2 keys");
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_L2, true, s_timeNs);
    CHECK(StateOf(context, slot).axes[mwin_padTriggerLeft] == 1.0f &&
              StateOf(context, slot).buttons == 0,
          "L2 pressed as the left trigger at 1");
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_L2, false, s_timeNs);
    CHECK(StateOf(context, slot).axes[mwin_padTriggerLeft] == 0.0f, "and let go at 0");
    Drain(program, context);
}

static void CheckRaw(Program* program, mwinContext* context)
{
    static const int32_t axes[] = {AXIS_X, AXIS_Y, AXIS_HAT_X};
    uint64_t keys = KeyBit(KEY_BUTTON_1) | KeyBit(KEY_BUTTON_1 + 1) | KeyBit(KEY_BUTTON_1 + 2) |
                    KeyBit(KEY_BUTTON_1 + 5);
    uint32_t slot = 0;
    mwinAndroidPadLayout layout = Layout(context, keys, axes, 3, &slot);
    mwinGamepadInfo info = {0};
    (void)mwinGetGamepadInfo(context, mwinGamepadIdOf(context, slot), &info);
    CHECK(!layout.mapped && !info.mapped && info.rawButtons == 4 && info.rawAxes == 3,
          "no south face button: raw, with its buttons and axes counted");
    Drain(program, context);
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_1 + 5, true, s_timeNs);
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_1 + 3, true, s_timeNs);
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_1, true, s_timeNs);
    mwinAndroidPadKey(context, slot, &layout, KEY_BUTTON_1 + 1, true, s_timeNs);
    const float values[] = {0.25f, -0.5f, 1.0f};
    mwinAndroidPadAxes(context, slot, &layout, values, s_timeNs);
    Drain(program, context);
    bool raw = true;
    for (int i = 0; i < 3; i++)
    {
        raw = raw && program->events[i].type == mwin_eventGamepadButtonDown &&
              program->events[i].data.gamepadButton.raw;
    }
    CHECK(program->eventCount == 6 && raw && program->events[0].data.gamepadButton.button == 3 &&
              program->events[1].data.gamepadButton.button == 0 &&
              program->events[2].data.gamepadButton.button == 1,
          "BUTTON_6, the fourth key it has, as raw button 3, BUTTON_1 as 0 and BUTTON_2 as 1; "
          "a key it lacks left out");
    mwinGamepadState state = StateOf(context, slot);
    CHECK(state.axes[0] == 0.25f && state.axes[1] == -0.5f && state.axes[2] == 1.0f,
          "its axes in order, with Android's values");
}

static void Step(Program* program, mwinContext* context, int step)
{
    switch (step)
    {
    case 0:
        CheckKnown(program, context);
        CheckGeneric(program, context);
        CheckTriggers(program, context);
        CheckRaw(program, context);
        program->done = true;
        break;
    default:
        break;
    }
}

int main(void)
{
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
