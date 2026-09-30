// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pens on macOS against AppKit (a CI runner's session): a pen hovering,
// touching down with its pressure and tilt, dragging, its barrel button
// pressed and released, lifting; then its eraser end near the tablet.
// None of it may make a mouse record. No tablet is attached to the
// runner, so the test makes the tablet events through Quartz, the only
// way to give them the tablet subtype, and hands them to the view. Such
// events have no window, so their places are not checked.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <math.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull
#define MAX_RECORDS 32

// A tablet event's buttons, and Quartz's pointer type of an eraser.
#define TIP    1
#define BARREL 2
#define ERASER 3

typedef struct Record
{
    mwinEventType type;
    mwinPenEvent pen;
} Record;

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    Record records[MAX_RECORDS];
    size_t count;
    bool mouse;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        program->mouse |= event.type >= mwin_eventCursorMoved && event.type <= mwin_eventWheel;
        bool pen = event.type >= mwin_eventPenMoved && event.type <= mwin_eventPenButtonUp;
        if (pen && program->count < MAX_RECORDS)
        {
            program->records[program->count++] = (Record){event.type, event.data.pen};
        }
    }
}

// A pen's mouse event: the tablet point subtype, with pressure, tilt
// and the tablet's buttons.
static NSEvent* Pen(CGEventType type, double pressure, double tiltX, double tiltY, int64_t buttons)
{
    CGEventRef made =
        CGEventCreateMouseEvent(nullptr, type, CGPointMake(100.0, 100.0), kCGMouseButtonLeft);
    CGEventSetIntegerValueField(made, kCGMouseEventSubtype, kCGEventMouseSubtypeTabletPoint);
    CGEventSetDoubleValueField(made, kCGMouseEventPressure, pressure);
    CGEventSetDoubleValueField(made, kCGTabletEventPointPressure, pressure);
    CGEventSetDoubleValueField(made, kCGTabletEventTiltX, tiltX);
    CGEventSetDoubleValueField(made, kCGTabletEventTiltY, tiltY);
    CGEventSetIntegerValueField(made, kCGTabletEventPointButtons, buttons);
    NSEvent* event = [NSEvent eventWithCGEvent:made];
    CFRelease(made);
    return event;
}

static NSEvent* Proximity(int64_t pointer, bool entering)
{
    CGEventRef made = CGEventCreate(nullptr);
    CGEventSetType(made, kCGEventTabletProximity);
    CGEventSetIntegerValueField(made, kCGTabletProximityEventPointerType, pointer);
    CGEventSetIntegerValueField(made, kCGTabletProximityEventEnterProximity, entering ? 1 : 0);
    NSEvent* event = [NSEvent eventWithCGEvent:made];
    CFRelease(made);
    return event;
}

static void SendPen(NSView* view)
{
    [view mouseMoved:Pen(kCGEventMouseMoved, 0.0, 0.0, 0.0, 0)];
    [view mouseDown:Pen(kCGEventLeftMouseDown, 0.5, 0.5, 0.25, TIP)];
    [view mouseDragged:Pen(kCGEventLeftMouseDragged, 0.75, 0.5, 0.25, TIP)];
    [view mouseDragged:Pen(kCGEventLeftMouseDragged, 0.75, 0.5, 0.25, TIP | BARREL)];
    [view mouseDragged:Pen(kCGEventLeftMouseDragged, 0.75, 0.5, 0.25, TIP)];
    [view mouseUp:Pen(kCGEventLeftMouseUp, 0.0, 0.0, 0.0, 0)];
    [view tabletProximity:Proximity(ERASER, true)];
    [view mouseDown:Pen(kCGEventLeftMouseDown, 1.0, 0.0, 0.0, TIP)];
    [view mouseUp:Pen(kCGEventLeftMouseUp, 0.0, 0.0, 0.0, 0)];
    [view tabletProximity:Proximity(ERASER, false)];
}

static bool Near(float value, float expected)
{
    return fabsf(value - expected) < 0.01f;
}

static bool Is(const Program* program, size_t index, mwinEventType type, mwinPenFlags flags)
{
    return index < program->count && program->records[index].type == type &&
           program->records[index].pen.flags == flags;
}

static void CheckPen(const Program* program)
{
    const mwinPenFlags touching = mwin_penContact;
    CHECK(program->count == 10, "ten pen records");
    CHECK(Is(program, 0, mwin_eventPenMoved, 0) && program->records[0].pen.pressure == 0.0f,
          "hovering");
    const mwinPenEvent* down = &program->records[1].pen;
    CHECK(Is(program, 1, mwin_eventPenDown, touching) && Near(down->pressure, 0.5f) &&
              Near(down->tiltX, 45.0f) && Near(down->tiltY, -22.5f),
          "down, with its pressure and its tilt in degrees, y turned down");
    CHECK(Is(program, 2, mwin_eventPenMoved, touching) &&
              Near(program->records[2].pen.pressure, 0.75f),
          "a drag moves it touching");
    CHECK(Is(program, 3, mwin_eventPenButtonDown, touching | mwin_penBarrel) &&
              program->records[3].pen.button == 1 &&
              Is(program, 4, mwin_eventPenMoved, touching | mwin_penBarrel),
          "the barrel pressed, then the move");
    CHECK(Is(program, 5, mwin_eventPenButtonUp, touching) && program->records[5].pen.button == 1 &&
              Is(program, 6, mwin_eventPenMoved, touching),
          "the barrel released");
    CHECK(Is(program, 7, mwin_eventPenUp, 0), "lifted");
    CHECK(Is(program, 8, mwin_eventPenDown, touching | mwin_penEraser) &&
              Is(program, 9, mwin_eventPenUp, mwin_penEraser),
          "the eraser end");
    CHECK(!program->mouse, "no mouse record");
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS pen";
    def.titleLength = 14;
    def.size = (mwinSize){320.0f, 240.0f};
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (program->shown)
    {
        mwinNativeHandles handles;
        @autoreleasepool
        {
            if (program->phase == 0 &&
                mwinGetNativeHandles(context, program->window, &handles) == mwin_success)
            {
                program->count = 0;
                program->mouse = false;
                SendPen((NSView*)handles.handles.apple.view);
            }
            else if (program->phase == 1)
            {
                CheckPen(program);
            }
        }
        program->phase += 1;
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == 2 ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {0};
    setvbuf(stdout, nullptr, _IONBF, 0);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on macOS");
    CHECK(!program.timedOut, "the window shows in time");
    CHECK(program.phase == 2, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
