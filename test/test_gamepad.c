// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The gamepad contract against the test backend: hotplug records naming
// no window, facts with raw counts kept to what the state holds, a list
// in the order they came, mapped and raw buttons and axes posted only
// when they change, the state,
// changes merged per gamepad, rumble, trigger rumble apart from it,
// motion off until asked for with its angle summed over every sample
// and started anew by each read, ids stale after a disconnect,
// button records lost to a full ring answered by a reset, axis records
// merged, and the gamepad limit.

#include "test_program.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mwinGamepadInfo Info(const char* name, bool mapped)
{
    mwinGamepadInfo info = {0};
    info.nameLength = (uint32_t)strlen(name);
    memcpy(info.name, name, info.nameLength);
    info.vendor = 0x045E;
    info.product = 0x0B12;
    info.mapped = mapped;
    info.rawButtons = mapped ? 0 : 40;
    info.rawAxes = mapped ? 0 : 3;
    info.capabilities = mapped ? mwin_padRumble | mwin_padTriggerRumble | mwin_padMotion : 0;
    info.battery = -1;
    return info;
}

static bool SamePad(mwinGamepadId a, mwinGamepadId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// The gamepads a test connects.
static mwinGamepadId s_pads[3];

static void CheckAdded(Program* program, mwinContext* context)
{
    CHECK(program->eventCount == 2 && program->events[0].type == mwin_eventGamepadAdded &&
              program->events[1].type == mwin_eventGamepadAdded &&
              SamePad(program->events[1].data.gamepad, s_pads[1]) &&
              program->events[0].window.index1 == 0,
          "hotplug records name the gamepad and no window");
    mwinGamepadId listed[3];
    size_t count = 0;
    CHECK(mwinContext_GetGamepads(context, listed, 3, &count) == mwin_success && count == 2 &&
              SamePad(listed[0], s_pads[0]) && SamePad(listed[1], s_pads[1]),
          "a list in the order they came");
    CHECK(mwinContext_GetGamepads(context, listed, 1, &count) == mwin_success && count == 2 &&
              SamePad(listed[0], s_pads[0]),
          "a short list is the first, and says how many");
    // Exact heap lists, so that AddressSanitizer sees a write past one.
    mwinGamepadId* one = malloc(sizeof(mwinGamepadId));
    mwinGamepadId* two = malloc(2 * sizeof(mwinGamepadId));
    if (one != nullptr && two != nullptr)
    {
        CHECK(mwinContext_GetGamepads(context, one, 1, &count) == mwin_success && count == 2 &&
                  SamePad(one[0], s_pads[0]) &&
                  mwinContext_GetGamepads(context, two, 2, &count) == mwin_success && count == 2 &&
                  SamePad(two[1], s_pads[1]),
              "a list of one, and one that fits exactly");
    }
    free(one);
    free(two);
    mwinGamepadInfo info;
    CHECK(mwinGamepad_GetInfo(context, s_pads[0], &info) == mwin_success && info.mapped &&
              info.nameLength == 7 && memcmp(info.name, "Pad \xE2\x9C\x93", 7) == 0 &&
              info.vendor == 0x045E && info.rawButtons == 0 &&
              (info.capabilities & mwin_padRumble) != 0,
          "a mapped gamepad's facts");
    CHECK(mwinGamepad_GetInfo(context, s_pads[1], &info) == mwin_success && !info.mapped &&
              info.rawButtons == MWIN_GAMEPAD_RAW_BUTTONS && info.rawAxes == 3,
          "a raw gamepad's counts, kept to what the state holds");
}

static void Press(mwinContext* context)
{
    CHECK(
        mwinGamepad_SetTestButton(context, s_pads[0], mwin_padFaceSouth, true) == mwin_success &&
            mwinGamepad_SetTestButton(context, s_pads[0], mwin_padFaceSouth, true) ==
                mwin_success &&
            mwinGamepad_SetTestAxis(context, s_pads[0], mwin_padStickLeftX, 0.5f) == mwin_success &&
            mwinGamepad_SetTestAxis(context, s_pads[0], mwin_padStickLeftX, 0.5f) == mwin_success &&
            mwinGamepad_SetTestAxis(context, s_pads[0], mwin_padTriggerRight, 1.0f) ==
                mwin_success &&
            mwinGamepad_SetTestButton(context, s_pads[0], mwin_padFaceSouth, false) == mwin_success,
        "a mapped gamepad's controls");
    CHECK(mwinGamepad_SetTestButton(context, s_pads[1], 20, true) == mwin_success &&
              mwinGamepad_SetTestButton(context, s_pads[1], 40, true) == mwin_success &&
              mwinGamepad_SetTestAxis(context, s_pads[1], 2, -0.25f) == mwin_success &&
              mwinGamepad_SetTestAxis(context, s_pads[1], 5, 1.0f) == mwin_success,
          "a raw gamepad's controls");
}

static void CheckPressed(Program* program, mwinContext* context)
{
    static const mwinEventType types[] = {
        mwin_eventGamepadButtonDown, mwin_eventGamepadAxisMoved,  mwin_eventGamepadAxisMoved,
        mwin_eventGamepadButtonUp,   mwin_eventGamepadButtonDown, mwin_eventGamepadAxisMoved,
    };
    CHECK(Types(program, types, 6), "only changes, in order");
    const mwinEvent* events = program->events;
    CHECK(events[0].data.gamepadButton.button == mwin_padFaceSouth &&
              !events[0].data.gamepadButton.raw &&
              SamePad(events[0].data.gamepadButton.gamepad, s_pads[0]) &&
              events[1].data.gamepadAxis.axis == mwin_padStickLeftX &&
              events[1].data.gamepadAxis.value == 0.5f &&
              events[2].data.gamepadAxis.axis == mwin_padTriggerRight,
          "mapped records");
    CHECK(events[4].data.gamepadButton.button == 20 && events[4].data.gamepadButton.raw &&
              events[5].data.gamepadAxis.axis == 2 && events[5].data.gamepadAxis.raw &&
              events[5].data.gamepadAxis.value == -0.25f,
          "raw records");
    mwinGamepadState state;
    CHECK(mwinGamepad_GetState(context, s_pads[0], &state) == mwin_success && state.buttons == 0 &&
              state.axes[mwin_padStickLeftX] == 0.5f && state.axes[mwin_padTriggerRight] == 1.0f,
          "a mapped gamepad's state");
    CHECK(mwinGamepad_GetState(context, s_pads[1], &state) == mwin_success &&
              state.buttons == 1u << 20 && state.axes[2] == -0.25f,
          "a raw gamepad's state");
}

static void Rumble(mwinContext* context)
{
    float low = 0.0f;
    float high = 0.0f;
    uint32_t duration = 0;
    uint32_t count = 0;
    CHECK(mwinGamepad_SetRumble(context, s_pads[0], 0.25f, 1.0f, 300) == mwin_success &&
              mwinGamepad_SetRumble(context, s_pads[0], 1.0f, 0.5f, 100) == mwin_success &&
              mwinGamepad_GetTestRumble(context, s_pads[0], &low, &high, &duration, &count) ==
                  mwin_success &&
              low == 1.0f && high == 0.5f && duration == 100 && count == 2,
          "rumble, the latest wins");
    CHECK(mwinGamepad_SetRumble(context, s_pads[1], 0.5f, 0.5f, 100) == mwin_errorUnsupported,
          "no rumble without motors");
    CHECK(mwinGamepad_SetRumble(context, s_pads[0], 1.5f, 0.5f, 100) == mwin_errorInvalid &&
              mwinGamepad_SetRumble(context, s_pads[0], NAN, 0.5f, 100) == mwin_errorInvalid,
          "strengths from 0 to 1");
    CHECK(mwinGamepad_SetTriggerRumble(context, s_pads[0], 0.75f, 0.0f, 200) == mwin_success &&
              mwinGamepad_GetTestTriggerRumble(context, s_pads[0], &low, &high, &duration,
                                               &count) == mwin_success &&
              low == 0.75f && high == 0.0f && duration == 200 && count == 1,
          "trigger rumble");
    CHECK(mwinGamepad_GetTestRumble(context, s_pads[0], &low, &high, &duration, &count) ==
                  mwin_success &&
              low == 1.0f && count == 2,
          "apart from the motors' rumble");
    CHECK(mwinGamepad_SetTriggerRumble(context, s_pads[1], 0.5f, 0.5f, 100) ==
                  mwin_errorUnsupported &&
              mwinGamepad_SetTriggerRumble(context, s_pads[0], -0.1f, 0.5f, 100) ==
                  mwin_errorInvalid &&
              mwinGamepad_SetTriggerRumble(nullptr, s_pads[0], 0.5f, 0.5f, 100) ==
                  mwin_errorInvalid,
          "no trigger rumble without trigger motors; strengths from 0 to 1");
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static void Motion(mwinContext* context)
{
    const float still[3] = {0.0f, 9.80665f, 0.0f};
    const float turning[3] = {1.0f, -2.0f, 0.5f};
    mwinGamepadMotion motion;
    bool on = true;
    bool enabled = true;
    CHECK(mwinGamepad_PostTestMotion(context, s_pads[0], still, turning, 1000) == mwin_success &&
              mwinGamepad_GetMotion(context, s_pads[0], &motion) == mwin_success &&
              motion.timeNs == 0 && motion.rotation[1] == 0.0f &&
              mwinGamepad_IsTestMotionEnabled(context, s_pads[0], &on) == mwin_success && !on &&
              mwinGamepad_IsMotionEnabled(context, s_pads[0], &enabled) == mwin_success && !enabled,
          "motion off when connected: samples left out");
    CHECK(mwinGamepad_EnableMotion(context, s_pads[0], true) == mwin_success &&
              mwinGamepad_IsTestMotionEnabled(context, s_pads[0], &on) == mwin_success && on &&
              mwinGamepad_IsMotionEnabled(context, s_pads[0], &enabled) == mwin_success && enabled,
          "motion turned on");
    // Four samples 10 ms apart: the first only starts the clock.
    for (uint64_t i = 0; i < 4; i++)
    {
        CHECK(mwinGamepad_PostTestMotion(context, s_pads[0], still, turning,
                                         5000000 + i * 10000000) == mwin_success,
              "a sample");
    }
    CHECK(mwinGamepad_GetMotion(context, s_pads[0], &motion) == mwin_success &&
              motion.acceleration[1] == 9.80665f && motion.rotationRate[1] == -2.0f &&
              Near(motion.rotation[0], 0.03f) && Near(motion.rotation[1], -0.06f) &&
              Near(motion.rotation[2], 0.015f) && motion.timeNs == 35000000,
          "the latest sample, and the angle summed over 30 ms");
    // A second later: a gap, which turns the pad by 100 ms of rate only.
    CHECK(mwinGamepad_PostTestMotion(context, s_pads[0], still, turning, 1035000000) ==
                  mwin_success &&
              mwinGamepad_GetMotion(context, s_pads[0], &motion) == mwin_success &&
              Near(motion.rotation[0], 0.1f) && motion.timeNs == 1035000000,
          "each read starts the angle anew; a gap counts 100 ms");
    CHECK(mwinGamepad_EnableMotion(context, s_pads[0], false) == mwin_success &&
              mwinGamepad_GetMotion(context, s_pads[0], &motion) == mwin_success &&
              motion.timeNs == 0 && motion.acceleration[1] == 0.0f &&
              mwinGamepad_IsTestMotionEnabled(context, s_pads[0], &on) == mwin_success && !on &&
              mwinGamepad_IsMotionEnabled(context, s_pads[0], &enabled) == mwin_success && !enabled,
          "turned off: all zero");
    CHECK(mwinGamepad_EnableMotion(context, s_pads[1], true) == mwin_errorUnsupported &&
              mwinGamepad_GetMotion(context, s_pads[1], &motion) == mwin_errorUnsupported &&
              mwinGamepad_GetMotion(context, s_pads[0], nullptr) == mwin_errorInvalid &&
              mwinGamepad_EnableMotion(nullptr, s_pads[0], true) == mwin_errorInvalid &&
              mwinGamepad_IsMotionEnabled(context, s_pads[1], &enabled) == mwin_success &&
              !enabled &&
              mwinGamepad_IsMotionEnabled(context, s_pads[0], nullptr) == mwin_errorInvalid &&
              mwinGamepad_IsMotionEnabled(nullptr, s_pads[0], &enabled) == mwin_errorInvalid &&
              mwinGamepad_PostTestMotion(context, s_pads[0], nullptr, turning, 0) ==
                  mwin_errorInvalid,
          "no motion without sensors; NULL refused");
}

static void HotplugStep(Program* program, mwinContext* context, int step)
{
    if (step == 0)
    {
        mwinGamepadInfo mapped = Info("Pad \xE2\x9C\x93", true);
        mwinGamepadInfo raw = Info("Stick", false);
        CHECK(mwinAddTestGamepad(context, &mapped, &s_pads[0]) == mwin_success &&
                  mwinAddTestGamepad(context, &raw, &s_pads[1]) == mwin_success,
              "two gamepads");
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        CheckAdded(program, context);
        program->windows[0] = Create(context, nullptr);
        return;
    }
    if (step == 2)
    {
        Press(context);
        return;
    }
    if (step == 3)
    {
        CheckPressed(program, context);
        Rumble(context);
        Motion(context);
        mwinGamepadInfo charged = Info("Pad \xE2\x9C\x93", true);
        charged.battery = 50;
        CHECK(mwinGamepad_SetTestInfo(context, s_pads[0], &charged) == mwin_success &&
                  mwinGamepad_SetTestInfo(context, s_pads[0], &charged) == mwin_success &&
                  mwinRemoveTestGamepad(context, s_pads[1]) == mwin_success,
              "a change, twice, and a disconnect");
        mwinGamepadInfo info;
        bool enabled = false;
        CHECK(mwinGamepad_GetInfo(context, s_pads[1], &info) == mwin_errorStale &&
                  mwinGamepad_IsMotionEnabled(context, s_pads[1], &enabled) == mwin_errorStale &&
                  mwinGamepad_SetTestButton(context, s_pads[1], 1, true) == mwin_errorStale,
              "a disconnected gamepad's id is stale at once");
        mwinGamepadInfo again = Info("Again", false);
        CHECK(mwinAddTestGamepad(context, &again, &s_pads[2]) == mwin_success &&
                  s_pads[2].index1 != s_pads[1].index1,
              "its slot waits for its record to be drained");
        return;
    }
    static const mwinEventType types[] = {mwin_eventGamepadChanged, mwin_eventGamepadRemoved,
                                          mwin_eventGamepadAdded};
    CHECK(Types(program, types, 3) && SamePad(program->events[0].data.gamepad, s_pads[0]),
          "changes merged, then the disconnect and a new gamepad");
    mwinGamepadInfo info;
    CHECK(mwinGamepad_GetInfo(context, s_pads[0], &info) == mwin_success && info.battery == 50,
          "the battery");
    mwinGamepadId reused;
    CHECK(mwinAddTestGamepad(context, &info, &reused) == mwin_success &&
              reused.index1 == s_pads[1].index1 && reused.generation == s_pads[1].generation + 1,
          "drained, the slot takes a new gamepad under a new generation");
    program->done = true;
}

static void TestHotplug(void)
{
    Program program = {.step = HotplugStep};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
}

static void OverflowStep(Program* program, mwinContext* context, int step)
{
    if (step == 0)
    {
        mwinGamepadInfo info = Info("Pad", true);
        CHECK(mwinAddTestGamepad(context, &info, &s_pads[0]) == mwin_success &&
                  mwinAddTestGamepad(context, &info, &s_pads[1]) == mwin_success,
              "two gamepads");
        // Presses and releases in turn, twelve records for a ring of four.
        for (int i = 0; i < 12; i++)
        {
            (void)mwinGamepad_SetTestButton(context, s_pads[i % 2], mwin_padFaceEast,
                                            (i / 2) % 2 == 0);
        }
        // Two axes in turn: each merges into its own.
        for (int i = 1; i <= 10; i++)
        {
            mwinGamepadAxis axis = i % 2 == 1 ? mwin_padStickRightX : mwin_padStickRightY;
            (void)mwinGamepad_SetTestAxis(context, s_pads[0], axis, (float)i / 10.0f);
        }
        return;
    }
    Drain(program, context);
    int buttons = 0;
    int resets = 0;
    float x = 0.0f;
    float y = 0.0f;
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        const mwinGamepadAxisEvent* axis = &event->data.gamepadAxis;
        buttons +=
            event->type == mwin_eventGamepadButtonDown || event->type == mwin_eventGamepadButtonUp;
        x = event->type == mwin_eventGamepadAxisMoved && axis->axis == mwin_padStickRightX
                ? axis->value
                : x;
        y = event->type == mwin_eventGamepadAxisMoved && axis->axis == mwin_padStickRightY
                ? axis->value
                : y;
        resets += event->type == mwin_eventInputStateReset && event->window.index1 == 0;
    }
    CHECK(buttons == 4 && resets == 2,
          "button records past the ring are lost, and each gamepad reset");
    CHECK(x == 0.9f && y == 1.0f, "axis records past the ring merge per axis, the latest kept");
    mwinGamepadState state;
    CHECK(mwinGamepad_GetState(context, s_pads[1], &state) == mwin_success && state.buttons == 0,
          "the state holds what the records lost");
    program->done = true;
}

static void TestOverflow(void)
{
    Program program = {.step = OverflowStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.inputPerWindow = 4;
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
}

static void LimitStep(Program* program, mwinContext* context, int step)
{
    (void)step;
    mwinGamepadInfo info = Info("Pad", true);
    mwinGamepadId id;
    CHECK(mwinAddTestGamepad(context, &info, &s_pads[0]) == mwin_success, "one gamepad");
    CHECK(mwinAddTestGamepad(context, &info, &id) == mwin_errorCapacity, "not two");
    info.name[0] = (char)0xFF;
    info.nameLength = 1;
    CHECK(mwinGamepad_SetTestInfo(context, s_pads[0], &info) == mwin_success &&
              mwinGamepad_GetInfo(context, s_pads[0], &info) == mwin_success &&
              info.nameLength == 0,
          "a name that is not UTF-8 is left empty");
    memset(info.name, 'p', 62);
    memcpy(info.name + 62, "\xE2\x9C", 2);
    info.nameLength = MWIN_GAMEPAD_NAME_BYTES;
    CHECK(mwinGamepad_SetTestInfo(context, s_pads[0], &info) == mwin_success &&
              mwinGamepad_GetInfo(context, s_pads[0], &info) == mwin_success &&
              info.nameLength == 62 && info.name[61] == 'p',
          "a name cut inside a character keeps its whole characters");
    memset(info.name, 'p', MWIN_GAMEPAD_NAME_BYTES);
    info.nameLength = MWIN_GAMEPAD_NAME_BYTES + 1;
    CHECK(mwinGamepad_SetTestInfo(context, s_pads[0], &info) == mwin_success &&
              mwinGamepad_GetInfo(context, s_pads[0], &info) == mwin_success &&
              info.nameLength == MWIN_GAMEPAD_NAME_BYTES,
          "a length past the name's bytes read as the name's bytes");
    CHECK(mwinGamepad_GetInfo(context, (mwinGamepadId){9, 1}, &info) == mwin_errorStale &&
              mwinGamepad_GetInfo(context, s_pads[0], nullptr) == mwin_errorInvalid,
          "unknown ids and missing arguments");
    program->done = true;
}

static void TestLimit(void)
{
    Program program = {.step = LimitStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.gamepads = 1;
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    def.limits.gamepads = 100;
    CHECK(RunWith(&program, def) == mwin_errorInvalid, "notifications must cover the gamepads");
}

int main(void)
{
    TestHotplug();
    TestOverflow();
    TestLimit();
    return s_failures == 0 ? 0 : 1;
}
