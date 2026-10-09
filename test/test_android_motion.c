// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Android's pointers turned into records (android_motion.h), on any
// platform, with samples as the backend reads them from motion events,
// posted to a window of the test backend: touches with ids never reused,
// only moved fingers told, a cancel ending every touch, pressure kept to
// 0..1; the pen hovering, touching, its barrel and eraser (from a source
// that is a mouse's too), tilts toward x and y from Android's tilt and
// orientation; the cursor entering at its first motion and never leaving
// on a hover's end, buttons from the state's changes (Back and Forward
// too), quick clicks within the double tap's time, a press with no
// button state as the primary button and the pointer's going up
// releasing it, the wheel; and everything forgotten.

#include "android_motion.h"
#include "test_program.h"

#include <math.h>

#define PI 3.14159265358979f

static uint32_t s_slot;
static mwinAndroidPointers s_pointers = {.doubleClickNs = 300000000u};
static uint64_t s_timeNs = 1000000000u;

static mwinAndroidPointer Finger(int32_t id, float x, float y, float pressure)
{
    return (mwinAndroidPointer){id, mwin_androidFinger, {x, y}, pressure, 0.0f, 0.0f};
}

static mwinAndroidPointer Stylus(mwinAndroidTool tool, float x, float y, float tilt,
                                 float orientation)
{
    return (mwinAndroidPointer){0, tool, {x, y}, 0.5f, tilt, orientation};
}

// Posts a sample of one or two pointers, 10 ms after the last.
static void Sample(mwinContext* context, mwinAndroidAction action, uint32_t index, bool mouse,
                   uint32_t buttons, const mwinAndroidPointer* first,
                   const mwinAndroidPointer* second)
{
    s_timeNs += 10000000u;
    mwinAndroidMotion motion = {.action = action,
                                .index = index,
                                .mouse = mouse,
                                .buttons = buttons,
                                .timeNs = s_timeNs,
                                .count = second != nullptr ? 2 : 1};
    motion.pointers[0] = *first;
    if (second != nullptr)
    {
        motion.pointers[1] = *second;
    }
    mwinAndroidMotionSample(context, s_slot, &s_pointers, &motion);
}

static void Mouse(mwinContext* context, mwinAndroidAction action, uint32_t buttons, float x,
                  float y)
{
    mwinAndroidPointer pointer = {0, mwin_androidMouse, {x, y}, 0.0f, 0.0f, 0.0f};
    Sample(context, action, 0, true, buttons, &pointer, nullptr);
}

static void CheckTouches(Program* program, mwinContext* context)
{
    mwinAndroidPointer a = Finger(5, 10.0f, 20.0f, 1.5f);
    mwinAndroidPointer b = Finger(7, 30.0f, 40.0f, 0.25f);
    Sample(context, mwin_androidDown, 0, false, 0, &a, nullptr);
    Sample(context, mwin_androidPointerDown, 1, false, 0, &a, &b);
    a.position.x = 12.0f;
    Sample(context, mwin_androidMove, 0, false, 0, &a, &b);
    Sample(context, mwin_androidPointerUp, 0, false, 0, &a, &b);
    // Android's id 5 again: a new touch.
    mwinAndroidPointer c = Finger(5, 50.0f, 60.0f, 0.5f);
    Sample(context, mwin_androidPointerDown, 1, false, 0, &b, &c);
    Sample(context, mwin_androidCancel, 0, false, 0, &b, &c);
    Drain(program, context);
    static const mwinEventType types[] = {
        mwin_eventTouchDown, mwin_eventTouchDown,      mwin_eventTouchMoved,    mwin_eventTouchUp,
        mwin_eventTouchDown, mwin_eventTouchCancelled, mwin_eventTouchCancelled};
    CHECK(Types(program, types, 7), "down, down, the moved one, up, down, both cancelled");
    if (program->eventCount != 7)
    {
        return;
    }
    const mwinEvent* events = program->events;
    CHECK(events[0].data.touch.id == 1 && events[1].data.touch.id == 2 &&
              events[2].data.touch.id == 1 && events[3].data.touch.id == 1 &&
              events[4].data.touch.id == 3,
          "ids counting up, Android's reused id a new touch");
    CHECK(events[0].data.touch.pressure == 1.0f && events[1].data.touch.pressure == 0.25f,
          "pressure kept to 0..1");
    CHECK(events[2].data.touch.position.x == 12.0f && events[0].data.touch.position.y == 20.0f,
          "positions as sampled");
    CHECK(events[0].timeNs + 20000000u == events[2].timeNs, "the samples' times");
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static void CheckPen(Program* program, mwinContext* context)
{
    mwinAndroidPointer pen = Stylus(mwin_androidStylus, 5.0f, 6.0f, PI / 4.0f, 0.0f);
    Sample(context, mwin_androidHoverMove, 0, false, 0, &pen, nullptr);
    Sample(context, mwin_androidDown, 0, false, 0, &pen, nullptr);
    pen.orientation = PI / 2.0f;
    Sample(context, mwin_androidMove, 0, false, mwin_androidStylusPrimary, &pen, nullptr);
    Sample(context, mwin_androidUp, 0, false, mwin_androidStylusPrimary, &pen, nullptr);
    mwinAndroidPointer eraser = Stylus(mwin_androidEraser, 5.0f, 6.0f, 0.0f, 0.0f);
    // From a tablet whose source is a mouse's too: still the pen.
    Sample(context, mwin_androidHoverMove, 0, true, 0, &eraser, nullptr);
    Drain(program, context);
    static const mwinEventType types[] = {
        mwin_eventPenMoved, mwin_eventPenDown,     mwin_eventPenButtonDown, mwin_eventPenMoved,
        mwin_eventPenUp,    mwin_eventPenButtonUp, mwin_eventPenMoved};
    CHECK(Types(program, types, 7), "hover, down, the barrel, moved, up, the barrel let go");
    if (program->eventCount != 7)
    {
        return;
    }
    const mwinEvent* events = program->events;
    CHECK(events[0].data.pen.flags == 0 && events[0].data.pen.pressure == 0.0f &&
              Near(events[0].data.pen.tiltX, 0.0f) && Near(events[0].data.pen.tiltY, 45.0f),
          "hovering, tilted toward positive y when it points up");
    CHECK(events[1].data.pen.flags == mwin_penContact && events[1].data.pen.pressure == 0.5f,
          "touching, with its pressure");
    CHECK(events[2].data.pen.button == 1 &&
              events[3].data.pen.flags == (mwin_penContact | mwin_penBarrel) &&
              Near(events[3].data.pen.tiltX, -45.0f) && Near(events[3].data.pen.tiltY, 0.0f),
          "the barrel held; pointing right, tilted toward negative x");
    CHECK(events[6].data.pen.flags == mwin_penEraser, "the eraser");
}

static void CheckCursor(Program* program, mwinContext* context)
{
    Mouse(context, mwin_androidHoverMove, 0, 5.0f, 5.0f);
    Mouse(context, mwin_androidHoverExit, 0, 5.0f, 5.0f);
    Mouse(context, mwin_androidButtonPress, mwin_androidPrimary, 5.0f, 5.0f);
    Mouse(context, mwin_androidMove, mwin_androidPrimary, 6.0f, 5.0f);
    Mouse(context, mwin_androidButtonRelease, 0, 6.0f, 5.0f);
    Mouse(context, mwin_androidButtonPress, mwin_androidPrimary | mwin_androidSecondary, 6.0f,
          5.0f);
    Mouse(context, mwin_androidButtonRelease, mwin_androidSecondary, 6.0f, 5.0f);
    Mouse(context, mwin_androidUp, 0, 6.0f, 5.0f);
    Drain(program, context);
    static const mwinEventType types[] = {
        mwin_eventCursorEntered, mwin_eventCursorMoved, mwin_eventButtonDown,
        mwin_eventCursorMoved,   mwin_eventButtonUp,    mwin_eventButtonDown,
        mwin_eventButtonDown,    mwin_eventButtonUp,    mwin_eventButtonUp};
    CHECK(Types(program, types, 9),
          "entered, moved, no leaving, presses and releases from the state's changes");
    if (program->eventCount != 9)
    {
        return;
    }
    const mwinEvent* events = program->events;
    CHECK(events[2].data.pointer.button == mwin_buttonLeft && events[2].data.pointer.clicks == 1 &&
              events[2].data.pointer.buttons == 1 && events[3].data.pointer.buttons == 1,
          "the left button held, one click");
    CHECK(events[5].data.pointer.button == mwin_buttonLeft && events[5].data.pointer.clicks == 2 &&
              events[6].data.pointer.button == mwin_buttonRight &&
              events[6].data.pointer.buttons == 3,
          "a second click within the double tap's time; both buttons");
    CHECK(events[7].data.pointer.button == mwin_buttonLeft &&
              events[8].data.pointer.button == mwin_buttonRight &&
              events[8].data.pointer.buttons == 0,
          "the left released by the state, the right by the pointer going up");
    // An injected press: no button state.
    s_timeNs += 1000000000u;
    Mouse(context, mwin_androidDown, 0, 6.0f, 5.0f);
    Mouse(context, mwin_androidUp, 0, 6.0f, 5.0f);
    mwinAndroidMotion scroll = {.action = mwin_androidScroll,
                                .mouse = true,
                                .scroll = {-0.5f, 1.0f},
                                .timeNs = s_timeNs,
                                .count = 1};
    scroll.pointers[0] = (mwinAndroidPointer){0, mwin_androidMouse, {6.0f, 5.0f}, 0, 0, 0};
    mwinAndroidMotionSample(context, s_slot, &s_pointers, &scroll);
    Drain(program, context);
    static const mwinEventType later[] = {mwin_eventButtonDown, mwin_eventButtonUp,
                                          mwin_eventWheel};
    CHECK(Types(program, later, 3) && program->events[0].data.pointer.button == mwin_buttonLeft &&
              program->events[0].data.pointer.clicks == 1 &&
              program->events[2].data.wheel.x == -0.5f && program->events[2].data.wheel.y == 1.0f,
          "a press without a state the left button; a late one a single click; the wheel");
}

// The side buttons: Back released by the state, Forward, the last, by
// the pointer going up.
static void CheckSideButtons(Program* program, mwinContext* context)
{
    s_timeNs += 1000000000u;
    Mouse(context, mwin_androidButtonPress, mwin_androidBack | mwin_androidForward, 6.0f, 5.0f);
    Mouse(context, mwin_androidButtonRelease, mwin_androidForward, 6.0f, 5.0f);
    Mouse(context, mwin_androidUp, 0, 6.0f, 5.0f);
    Drain(program, context);
    static const mwinEventType types[] = {mwin_eventButtonDown, mwin_eventButtonDown,
                                          mwin_eventButtonUp, mwin_eventButtonUp};
    const mwinEvent* events = program->events;
    CHECK(Types(program, types, 4) && events[0].data.pointer.button == mwin_buttonBack &&
              events[1].data.pointer.button == mwin_buttonForward &&
              events[1].data.pointer.buttons == 0x18 &&
              events[2].data.pointer.button == mwin_buttonBack &&
              events[3].data.pointer.button == mwin_buttonForward &&
              events[3].data.pointer.buttons == 0,
          "Back and Forward pressed; Back released by the state, Forward by the pointer up");
}

static void CheckForgetting(Program* program, mwinContext* context)
{
    mwinAndroidPointer a = Finger(1, 1.0f, 1.0f, 0.5f);
    Sample(context, mwin_androidDown, 0, false, 0, &a, nullptr);
    Mouse(context, mwin_androidButtonPress, mwin_androidPrimary, 5.0f, 5.0f);
    mwinAndroidForgetPointers(&s_pointers);
    Drain(program, context);
    Sample(context, mwin_androidUp, 0, false, 0, &a, nullptr);
    Mouse(context, mwin_androidButtonPress, mwin_androidPrimary, 5.0f, 5.0f);
    Drain(program, context);
    static const mwinEventType types[] = {mwin_eventCursorEntered, mwin_eventButtonDown};
    CHECK(Types(program, types, 2),
          "a forgotten touch's end left out; the cursor entering again, its button pressed anew");
}

static void Step(Program* program, mwinContext* context, int step)
{
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        s_slot = program->windows[0].index1 - 1;
        break;
    case 1:
        Drain(program, context);
        CheckTouches(program, context);
        CheckPen(program, context);
        CheckCursor(program, context);
        CheckSideButtons(program, context);
        CheckForgetting(program, context);
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
