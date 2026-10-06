// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The pads a runtime lists and reads (src/pad_tracker.c) against a
// stand-in for the runtime, driven on a context of the test backend:
// pads found at once when the runtime says one came, and otherwise not
// sooner than every half second; more than four; their facts and
// batteries, and a battery's change; their buttons, sticks and
// triggers, read only when the reading's time moves, and kept in their
// ranges; rumble, stopped when its time runs out; a removal; and every
// reference the runtime gave let go.

#include "pad_tracker.h"
#include "test_program.h"

#include <math.h>
#include <string.h>

#define PADS 6

// The stand-in's pads: which are connected, their readings, their
// batteries, and the references it has given out and not had back.
typedef struct Fake
{
    bool connected[PADS];
    mwinPadReading readings[PADS];
    int8_t batteries[PADS];
    float low;
    float high;
    float left;
    float right;
    bool told;
    int lists;
    int references;
} Fake;

static Fake s_fake;
static mwinPadTracker s_xbox;
// A pad is its index's address here, as the runtime's is its object's.
static char s_pads[PADS];

static int32_t List(void* self, void** pads, uint32_t capacity)
{
    (void)self;
    s_fake.lists += 1;
    uint32_t count = 0;
    for (int i = 0; i < PADS && count < capacity; i++)
    {
        if (s_fake.connected[i])
        {
            pads[count++] = &s_pads[i];
            s_fake.references += 1;
        }
    }
    return (int32_t)count;
}

static void Release(void* self, void* pad)
{
    (void)self;
    (void)pad;
    s_fake.references -= 1;
}

static bool Read(void* self, void* pad, mwinPadReading* reading)
{
    (void)self;
    *reading = s_fake.readings[(char*)pad - s_pads];
    return true;
}

static bool Vibrate(void* self, void* pad, const float motors[4])
{
    (void)self;
    (void)pad;
    s_fake.low = motors[0];
    s_fake.high = motors[1];
    s_fake.left = motors[2];
    s_fake.right = motors[3];
    return true;
}

static void Describe(void* self, void* pad, mwinGamepadInfo* info)
{
    (void)self;
    static const char name[] = "Xbox Wireless Controller";
    memcpy(info->name, name, sizeof(name) - 1);
    info->nameLength = sizeof(name) - 1;
    info->vendor = 0x045e;
    info->product = (uint16_t)(0x0b12 + ((char*)pad - s_pads));
    info->capabilities = mwin_padRumble;
}

static int8_t Battery(void* self, void* pad)
{
    (void)self;
    return s_fake.batteries[(char*)pad - s_pads];
}

static bool Changed(void* self)
{
    (void)self;
    bool told = s_fake.told;
    s_fake.told = false;
    return told;
}

#define MS 1000000u

static void Connect(Program* program, mwinContext* context)
{
    (void)program;
    static const mwinPadRuntime runtime = {nullptr, List,     Release, Read,
                                           Vibrate, Describe, Battery, Changed};
    mwinPadTrackerStart(&s_xbox, context, &runtime);
    s_fake.connected[1] = true;
    s_fake.batteries[1] = 80;
    s_fake.batteries[5] = -1;
    s_fake.readings[1].timestamp = 7;
    mwinPadTrackerPump(&s_xbox, 1000 * MS);
}

static void CheckConnected(Program* program, mwinContext* context)
{
    CHECK(program->eventCount >= 1 && program->events[0].type == mwin_eventGamepadAdded,
          "a pad found");
    mwinGamepadInfo info;
    mwinGamepadId pad = program->events[0].data.gamepad;
    CHECK(mwinGetGamepadInfo(context, pad, &info) == mwin_success && info.mapped &&
              info.battery == 80 && (info.capabilities & mwin_padRumble) != 0 &&
              info.vendor == 0x045e && info.product == 0x0b13 && info.nameLength == 24,
          "its facts and battery");
    mwinPadReading* reading = &s_fake.readings[1];
    reading->timestamp = 8;
    reading->buttons = 1u << mwin_padFaceSouth | 1u << mwin_padDpadUp | 1u << mwin_padGuide;
    reading->axes[mwin_padStickLeftX] = 1.0f;
    reading->axes[mwin_padStickLeftY] = -0.5f;
    reading->axes[mwin_padStickRightX] = 2.0f;
    reading->axes[mwin_padStickRightY] = NAN;
    reading->axes[mwin_padTriggerLeft] = -0.5f;
    reading->axes[mwin_padTriggerRight] = 1.0f;
    mwinPadTrackerPump(&s_xbox, 1010 * MS);
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

static void CheckPressed(Program* program, mwinContext* context)
{
    CHECK(Has(program, mwin_eventGamepadButtonDown, mwin_padFaceSouth, 0.0f) &&
              Has(program, mwin_eventGamepadButtonDown, mwin_padDpadUp, 0.0f) &&
              Has(program, mwin_eventGamepadButtonDown, mwin_padGuide, 0.0f),
          "the buttons held, by their bits");
    CHECK(Has(program, mwin_eventGamepadAxisMoved, mwin_padStickLeftX, 1.0f) &&
              Has(program, mwin_eventGamepadAxisMoved, mwin_padStickLeftY, -0.5f) &&
              Has(program, mwin_eventGamepadAxisMoved, mwin_padTriggerRight, 1.0f),
          "the axes as read");
    mwinGamepadState state;
    CHECK(mwinGetGamepadState(context, mwinGamepadIdOf(context, s_xbox.pads[0].slot), &state) ==
                  mwin_success &&
              state.axes[mwin_padStickRightX] == 1.0f && state.axes[mwin_padStickRightY] == 0.0f &&
              state.axes[mwin_padTriggerLeft] == 0.0f,
          "past the range kept in it, and a value that is not a number as 0");
    // The same time: nothing is read again.
    s_fake.readings[1].buttons = 0;
    mwinPadTrackerPump(&s_xbox, 1020 * MS);
}

static void CheckRumble(Program* program)
{
    CHECK(program->eventCount == 0, "an unchanged reading posts nothing");
    uint32_t slot = s_xbox.pads[0].slot;
    CHECK(mwinPadTrackerOwns(&s_xbox, slot) && !mwinPadTrackerOwns(&s_xbox, slot + 1),
          "the pad owns its slot");
    CHECK(mwinPadTrackerRumble(&s_xbox, slot, 1.0f, 0.5f, 100, 1030 * MS) == mwin_success &&
              s_fake.low == 1.0f && s_fake.high == 0.5f,
          "rumble, the heavy motor low");
    mwinPadTrackerPump(&s_xbox, 1080 * MS);
    CHECK(s_fake.low == 1.0f, "still running before its time");
    CHECK(mwinPadTrackerTriggerRumble(&s_xbox, slot, 0.25f, 0.75f, 200, 1090 * MS) ==
                  mwin_success &&
              s_fake.left == 0.25f && s_fake.right == 0.75f && s_fake.low == 1.0f,
          "the triggers' motors with the grips' still running");
    mwinPadTrackerPump(&s_xbox, 1140 * MS);
    CHECK(s_fake.low == 0.0f && s_fake.high == 0.0f && s_fake.right == 0.75f,
          "the grips stop when their time runs out, the triggers run on");
    mwinPadTrackerPump(&s_xbox, 1300 * MS);
    CHECK(s_fake.left == 0.0f && s_fake.right == 0.0f, "then the triggers stop");
    CHECK(mwinPadTrackerTriggerRumble(&s_xbox, slot, 0.5f, 0.5f, 0, 1300 * MS) == mwin_success &&
              s_fake.left == 0.0f &&
              mwinPadTrackerTriggerRumble(&s_xbox, slot + 1, 0.5f, 0.5f, 100, 1300 * MS) ==
                  mwin_errorPlatform,
          "a duration of 0 stops them; a slot it does not own is refused");
}

// Five more pads: the one the runtime names is found at once, the rest
// when the half second is up, and the core's eight slots are enough.
static void CheckSearch(Program* program)
{
    (void)program;
    for (int i = 2; i < PADS; i++)
    {
        s_fake.connected[i] = true;
    }
    s_fake.connected[0] = true;
    int lists = s_fake.lists;
    mwinPadTrackerPump(&s_xbox, 1150 * MS);
    CHECK(s_fake.lists == lists && s_xbox.count == 1, "not looked for each pump");
    s_fake.told = true;
    mwinPadTrackerPump(&s_xbox, 1160 * MS);
    CHECK(s_xbox.count == PADS, "but at once when the runtime says so");
    s_fake.batteries[1] = 40;
    mwinPadTrackerPump(&s_xbox, 1700 * MS);
}

static int Count(const Program* program, mwinEventType type)
{
    int count = 0;
    for (int i = 0; i < program->eventCount; i++)
    {
        count += program->events[i].type == type;
    }
    return count;
}

static void CheckMany(Program* program, mwinContext* context)
{
    CHECK(Count(program, mwin_eventGamepadAdded) == PADS - 1, "more than four pads");
    CHECK(Count(program, mwin_eventGamepadChanged) == 1, "a battery that changed is told");
    mwinGamepadInfo info;
    CHECK(mwinGetGamepadInfo(context, mwinGamepadIdOf(context, s_xbox.pads[0].slot), &info) ==
                  mwin_success &&
              info.battery == 40,
          "and read");
    CHECK(s_fake.references == PADS, "one reference kept for each pad");
    s_fake.connected[1] = false;
    s_fake.told = true;
    mwinPadTrackerPump(&s_xbox, 1710 * MS);
}

static void CheckRemoved(Program* program)
{
    CHECK(Count(program, mwin_eventGamepadRemoved) == 1 && s_xbox.count == PADS - 1 &&
              s_fake.references == PADS - 1,
          "a pad gone is removed and let go");
    mwinPadTrackerStop(&s_xbox);
    CHECK(s_fake.references == 0, "every reference let go at the stop");
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
        CheckPressed(program, context);
        break;
    case 3:
        CheckRumble(program);
        CheckSearch(program);
        break;
    case 4:
        CheckMany(program, context);
        break;
    default:
        CheckRemoved(program);
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
