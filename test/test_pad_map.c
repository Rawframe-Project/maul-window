// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Numbered gamepad controls through their mapping:
// - buttons from buttons, hat directions and axis halves;
// - sticks from axes, inverted, from an axis's half, and from buttons
//   driving their halves, held to -1 and 1 with an axis too;
// - triggers from a whole axis and from a half;
// - a raw gamepad's buttons, axes, and hats as two axes after them;
// - a device's range as an axis's, held to -1 and 1, and an empty one 0.

#include "pad_map.h"
#include "test_program.h"

#include "maul-window/gamepad.h"

#include <math.h>

static int32_t s_mapped = -1;
static int32_t s_raw = -1;

static bool Near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static mwinPadSource Button(uint8_t number)
{
    return (mwinPadSource)(mwin_padSourceButton << 14 | number);
}

static mwinPadSource Axis(uint8_t number, int half, bool inverted)
{
    return (mwinPadSource)(mwin_padSourceAxis << 14 | (inverted ? 0x400 : 0) | half << 8 | number);
}

static mwinPadSource Hat(uint8_t number, uint8_t direction)
{
    return (mwinPadSource)(mwin_padSourceHat << 14 | number << 4 | direction);
}

static void PostMapped(mwinContext* context)
{
    static mwinPadMapping mapping;
    static mwinPadSource halves[MWIN_PAD_HALVES];
    mapping.sources[mwin_padDpadUp] = Hat(0, 1);
    mapping.sources[mwin_padDpadDown] = Hat(0, 4);
    mapping.sources[mwin_padFaceSouth] = Button(0);
    mapping.sources[mwin_padFaceEast] = Axis(2, 1, false);
    mapping.sources[mwin_padFaceWest] = Axis(2, 2, false);
    mapping.sources[MWIN_GAMEPAD_BUTTONS + mwin_padStickLeftX] = Axis(0, 0, false);
    mapping.sources[MWIN_GAMEPAD_BUTTONS + mwin_padStickLeftY] = Axis(1, 0, true);
    mapping.sources[MWIN_GAMEPAD_BUTTONS + mwin_padStickRightY] = Axis(2, 1, false);
    mapping.sources[MWIN_GAMEPAD_BUTTONS + mwin_padTriggerLeft] = Axis(3, 0, false);
    mapping.sources[MWIN_GAMEPAD_BUTTONS + mwin_padTriggerRight] = Axis(4, 2, false);
    // The right stick's x from two buttons, its negative then its
    // positive half.
    halves[2 * mwin_padStickRightX] = Button(1);
    halves[2 * mwin_padStickRightX + 1] = Button(2);
    // The left stick's y has an axis and a half from a button both.
    halves[2 * mwin_padStickLeftY] = Button(2);
    mapping.halves = 1;
    mwinPadControls controls = {.mapping = &mapping,
                                .halves = halves,
                                .buttonCount = 3,
                                .axisCount = 5,
                                .hatCount = 1,
                                .buttons = {true, false, true},
                                .axes = {0.25f, 0.5f, 0.8f, 0.0f, -0.6f},
                                .hats = {1}};
    mwinPostPadControls(context, (uint32_t)s_mapped, &controls, 1);
}

static void PostRaw(mwinContext* context)
{
    mwinPadControls controls = {.buttonCount = 3,
                                .axisCount = 2,
                                .hatCount = 1,
                                .buttons = {false, true},
                                .axes = {0.5f, -0.25f},
                                .hats = {2 | 4}};
    mwinPostPadControls(context, (uint32_t)s_raw, &controls, 1);
}

static void CheckMapped(const mwinContext* context)
{
    mwinGamepadState state;
    CHECK(mwinGetGamepadState(context, mwinGamepadIdOf(context, (uint32_t)s_mapped), &state) ==
              mwin_success,
          "state");
    uint32_t expected = 1u << mwin_padDpadUp | 1u << mwin_padFaceSouth | 1u << mwin_padFaceEast;
    CHECK(state.buttons == expected,
          "buttons from a button, a hat direction and an axis half, the rest up");
    CHECK(Near(state.axes[mwin_padStickLeftX], 0.25f),
          "sticks from axes, inverted where the mapping says");
    CHECK(Near(state.axes[mwin_padStickLeftY], -1.0f),
          "a stick from an axis and a half from a button held to -1");
    CHECK(Near(state.axes[mwin_padStickRightX], 1.0f), "a stick's half from a button");
    CHECK(Near(state.axes[mwin_padStickRightY], 0.6f), "a stick from an axis's half");
    CHECK(Near(state.axes[mwin_padTriggerLeft], 0.5f) &&
              Near(state.axes[mwin_padTriggerRight], 0.6f),
          "triggers from a whole axis and from a half");
}

static void CheckRaw(const mwinContext* context)
{
    mwinGamepadState state;
    CHECK(mwinGetGamepadState(context, mwinGamepadIdOf(context, (uint32_t)s_raw), &state) ==
              mwin_success,
          "state");
    CHECK(state.buttons == 2u && Near(state.axes[0], 0.5f) && Near(state.axes[1], -0.25f) &&
              Near(state.axes[2], 1.0f) && Near(state.axes[3], 1.0f),
          "a raw gamepad's buttons and axes, and its hat as two axes after them");
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinGamepadInfo info = {.mapped = true};
    switch (step)
    {
    case 0:
        s_mapped = mwinAddGamepad(context, &info, 1);
        info = (mwinGamepadInfo){.rawButtons = 3, .rawAxes = 4};
        s_raw = mwinAddGamepad(context, &info, 1);
        CHECK(s_mapped >= 0 && s_raw >= 0, "two gamepads");
        PostMapped(context);
        PostRaw(context);
        break;
    default:
        CheckMapped(context);
        CheckRaw(context);
        CHECK(Near(mwinPadNormalize(5, 0, 10), 0.0f) && Near(mwinPadNormalize(0, 0, 10), -1.0f) &&
                  Near(mwinPadNormalize(10, 0, 10), 1.0f),
              "a device's range as an axis's");
        CHECK(Near(mwinPadNormalize(20, 0, 10), 1.0f) && Near(mwinPadNormalize(-5, 0, 10), -1.0f),
              "held to -1 and 1");
        CHECK(Near(mwinPadNormalize(3, 3, 3), 0.0f), "an empty range 0");
        program->done = true;
        break;
    }
}

int main(void)
{
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
