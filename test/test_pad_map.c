// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Numbered gamepad controls through their mapping:
// - buttons from buttons, hat directions and axis halves;
// - sticks from axes, inverted, from an axis's half, and from buttons
//   driving their halves, held to -1 and 1 with an axis too;
// - triggers from a whole axis and from a half;
// - a raw gamepad's buttons, axes, and hats as two axes after them;
// - controls past a pad's counts, which nothing reads;
// - a device's range as an axis's, held to -1 and 1, and an empty one 0;
// and a database's lookup: the exact version, else another, and none
// for another device or one past every entry.

#include "pad_map.h"
#include "test_program.h"

#include "maul-window/gamepad.h"

#include <math.h>
#include <stdlib.h>

static int32_t s_mapped = -1;
static int32_t s_raw = -1;
static int32_t s_shortMapped = -1;
static int32_t s_shortRaw = -1;

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

// Controls past a pad's counts hold stale values, which nothing reads: a
// mapping naming a button or an axis the device lacks, and a raw pad
// reporting fewer than its info says.
static void PostShort(mwinContext* context)
{
    static mwinPadMapping mapping;
    mapping.sources[mwin_padFaceSouth] = Button(3);
    mapping.sources[mwin_padFaceNorth] = Axis(5, 1, false);
    mapping.sources[MWIN_GAMEPAD_BUTTONS + mwin_padStickLeftX] = Axis(5, 0, false);
    mwinPadControls mapped = {.mapping = &mapping,
                              .buttonCount = 3,
                              .axisCount = 5,
                              .buttons = {false, false, false, true},
                              .axes = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f}};
    mwinPostPadControls(context, (uint32_t)s_shortMapped, &mapped, 1);
    mwinPadControls raw = {.buttonCount = 2,
                           .axisCount = 2,
                           .hatCount = 0,
                           .buttons = {false, true, true},
                           .axes = {0.5f, -0.25f, 0.75f},
                           .hats = {2}};
    mwinPostPadControls(context, (uint32_t)s_shortRaw, &raw, 1);
}

static void CheckShort(const mwinContext* context)
{
    mwinGamepadState mapped;
    mwinGamepadState raw;
    CHECK(mwinGetGamepadState(context, mwinGamepadIdOf(context, (uint32_t)s_shortMapped),
                              &mapped) == mwin_success &&
              mwinGetGamepadState(context, mwinGamepadIdOf(context, (uint32_t)s_shortRaw), &raw) ==
                  mwin_success,
          "states");
    CHECK(mapped.buttons == 0 && Near(mapped.axes[mwin_padStickLeftX], 0.0f),
          "a mapping past the device's buttons and axes reads nothing");
    CHECK(raw.buttons == 2u && Near(raw.axes[0], 0.5f) && Near(raw.axes[1], -0.25f) &&
              Near(raw.axes[2], 0.0f) && Near(raw.axes[3], 0.0f),
          "a raw pad's controls past its counts read nothing");
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
        info = (mwinGamepadInfo){.mapped = true};
        s_shortMapped = mwinAddGamepad(context, &info, 1);
        info = (mwinGamepadInfo){.rawButtons = 4, .rawAxes = 6};
        s_shortRaw = mwinAddGamepad(context, &info, 1);
        CHECK(s_shortMapped >= 0 && s_shortRaw >= 0, "two more");
        PostMapped(context);
        PostRaw(context);
        PostShort(context);
        break;
    default:
        CheckMapped(context);
        CheckRaw(context);
        CheckShort(context);
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

// The entries sit alone in a heap allocation of their count, so that
// AddressSanitizer sees a read past the last.
static void TestFindMapping(void)
{
    static const mwinPadMapping mappings[2] = {{.halves = 0}, {.halves = 0}};
    mwinPadEntry* entries = malloc(2 * sizeof(mwinPadEntry));
    if (entries == nullptr)
    {
        return;
    }
    entries[0] = (mwinPadEntry){3, 0x045E, 0x028E, 0x0110, 0};
    entries[1] = (mwinPadEntry){3, 0x054C, 0x09CC, 0x8111, 1};
    mwinPadDatabase database = {entries, 2, mappings, nullptr};
    CHECK(mwinFindPadMapping(&database, 3, 0x045E, 0x028E, 0x0110) == &mappings[0] &&
              mwinFindPadMapping(&database, 3, 0x054C, 0x09CC, 0x0100) == &mappings[1],
          "the exact version, else another");
    CHECK(mwinFindPadMapping(&database, 3, 0x045E, 0x028F, 0x0110) == nullptr &&
              mwinFindPadMapping(&database, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF) == nullptr,
          "none for another device, past every entry too");
    free(entries);
}

int main(void)
{
    TestFindMapping();
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
