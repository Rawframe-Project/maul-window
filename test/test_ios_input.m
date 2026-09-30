// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Touches, the Pencil and the pointer on iOS, in the simulator
// (tools/run_ios_app.sh). The test hands the view what UIKit would: its
// own touches and events for a finger's down, move and up and another's
// cancel, the Pencil touching with half its force at 45 degrees, the
// pointer's clicks with Shift and two buttons; its own recognizers for
// the pointer's hover and scrolling, through the actions the view gives
// them. The simulator cannot make these itself.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#import <UIKit/UIKit.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define MAX_RECORDS 64

@interface FakeTouch : UITouch
{
  @public
    CGPoint place;
    UITouchType kind;
    CGFloat pressed;
    NSUInteger taps;
}
@end

@implementation FakeTouch
- (CGPoint)locationInView:(UIView*)view
{
    (void)view;
    return place;
}

- (UITouchType)type
{
    return kind;
}

- (CGFloat)force
{
    return pressed;
}

- (CGFloat)maximumPossibleForce
{
    return 4.0;
}

- (CGFloat)altitudeAngle
{
    return M_PI / 4.0;
}

- (CGFloat)azimuthAngleInView:(UIView*)view
{
    (void)view;
    return 0.0;
}

- (NSUInteger)tapCount
{
    return taps;
}
@end

@interface FakeEvent : UIEvent
{
  @public
    UIEventButtonMask mask;
}
@end

@implementation FakeEvent
- (UIEventButtonMask)buttonMask
{
    return mask;
}

- (UIKeyModifierFlags)modifierFlags
{
    return UIKeyModifierShift;
}
@end

@interface FakeHover : UIHoverGestureRecognizer
{
  @public
    UIGestureRecognizerState now;
}
@end

@implementation FakeHover
- (UIGestureRecognizerState)state
{
    return now;
}

- (CGPoint)locationInView:(UIView*)view
{
    (void)view;
    return CGPointMake(50.0, 60.0);
}
@end

@interface FakePan : UIPanGestureRecognizer
@end

@implementation FakePan
- (CGPoint)translationInView:(UIView*)view
{
    (void)view;
    return CGPointMake(-20.0, 30.0);
}

- (void)setTranslation:(CGPoint)translation inView:(UIView*)view
{
    (void)translation;
    (void)view;
}
@end

// The actions the view gives its recognizers.
@interface UIView (MwinActions)
- (void)mwinHover:(UIHoverGestureRecognizer*)hover;
- (void)mwinScroll:(UIPanGestureRecognizer*)pan;
@end

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    int count;
    mwinEvent records[MAX_RECORDS];
    uint64_t fingers[2];
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static bool Input(mwinEventType type)
{
    return (type >= mwin_eventCursorMoved && type <= mwin_eventWheel) ||
           (type >= mwin_eventTouchDown && type <= mwin_eventPenButtonUp);
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        if (Input(event.type) && program->count < MAX_RECORDS)
        {
            program->records[program->count++] = event;
        }
    }
}

static UIView* ViewOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (UIView*)handles.handles.apple.view
               : nil;
}

static FakeTouch* Touch(UITouchType kind, CGFloat x, CGFloat y)
{
    FakeTouch* touch = [[[FakeTouch alloc] init] autorelease];
    touch->kind = kind;
    touch->place = CGPointMake(x, y);
    touch->taps = 1;
    return touch;
}

static void Send(UIView* view, SEL phase, UITouch* touch, UIEvent* event)
{
    NSSet* touches = [NSSet setWithObject:touch];
    ((void (*)(id, SEL, NSSet*, UIEvent*))[view methodForSelector:phase])(view, phase, touches,
                                                                          event);
}

static void Fingers(Program* program, UIView* view)
{
    FakeTouch* first = Touch(UITouchTypeDirect, 10.0, 20.0);
    FakeTouch* second = Touch(UITouchTypeDirect, 70.0, 80.0);
    program->fingers[0] = (uint64_t)(uintptr_t)first;
    program->fingers[1] = (uint64_t)(uintptr_t)second;
    Send(view, @selector(touchesBegan:withEvent:), first, nil);
    first->place = CGPointMake(30.0, 40.0);
    Send(view, @selector(touchesMoved:withEvent:), first, nil);
    Send(view, @selector(touchesEnded:withEvent:), first, nil);
    Send(view, @selector(touchesBegan:withEvent:), second, nil);
    Send(view, @selector(touchesCancelled:withEvent:), second, nil);
}

static void Pencil(UIView* view)
{
    FakeTouch* pencil = Touch(UITouchTypePencil, 100.0, 110.0);
    pencil->pressed = 2.0;
    Send(view, @selector(touchesBegan:withEvent:), pencil, nil);
    pencil->place = CGPointMake(105.0, 115.0);
    Send(view, @selector(touchesMoved:withEvent:), pencil, nil);
    Send(view, @selector(touchesEnded:withEvent:), pencil, nil);
}

static void Pointer(UIView* view)
{
    FakeTouch* pointer = Touch(UITouchTypeIndirectPointer, 200.0, 210.0);
    pointer->taps = 2;
    FakeEvent* event = [[[FakeEvent alloc] init] autorelease];
    event->mask = UIEventButtonMaskPrimary;
    Send(view, @selector(touchesBegan:withEvent:), pointer, event);
    event->mask = UIEventButtonMaskPrimary | UIEventButtonMaskSecondary;
    pointer->place = CGPointMake(220.0, 230.0);
    Send(view, @selector(touchesMoved:withEvent:), pointer, event);
    event->mask = 0;
    Send(view, @selector(touchesEnded:withEvent:), pointer, event);
    FakeHover* hover = [[[FakeHover alloc] initWithTarget:nil action:nil] autorelease];
    for (int i = 0; i < 2; i++)
    {
        hover->now = i == 0 ? UIGestureRecognizerStateBegan : UIGestureRecognizerStateEnded;
        [view mwinHover:hover];
    }
    FakePan* pan = [[[FakePan alloc] initWithTarget:nil action:nil] autorelease];
    [view mwinScroll:pan];
}

// The next record from *at of a type, or null.
static const mwinEvent* Next(const Program* program, int* at, mwinEventType type)
{
    while (*at < program->count && program->records[*at].type != type)
    {
        *at += 1;
    }
    return *at < program->count ? &program->records[(*at)++] : nullptr;
}

static bool TouchIs(const mwinEvent* event, uint64_t id, float x, float y)
{
    return event != nullptr && event->data.touch.id == id && event->data.touch.position.x == x &&
           event->data.touch.position.y == y && event->data.touch.pressure == -1.0f;
}

static void CheckFingers(const Program* program)
{
    int at = 0;
    const mwinEvent* down = Next(program, &at, mwin_eventTouchDown);
    const mwinEvent* moved = Next(program, &at, mwin_eventTouchMoved);
    const mwinEvent* up = Next(program, &at, mwin_eventTouchUp);
    CHECK(TouchIs(down, program->fingers[0], 10.0f, 20.0f) &&
              TouchIs(moved, program->fingers[0], 30.0f, 40.0f) &&
              TouchIs(up, program->fingers[0], 30.0f, 40.0f),
          "a finger down, moved and up, by one id, no pressure measured");
    const mwinEvent* cancelled = Next(program, &at, mwin_eventTouchCancelled);
    CHECK(TouchIs(cancelled, program->fingers[1], 70.0f, 80.0f), "another cancelled");
}

static void CheckPencil(const Program* program)
{
    int at = 0;
    const mwinEvent* down = Next(program, &at, mwin_eventPenDown);
    const mwinEvent* moved = Next(program, &at, mwin_eventPenMoved);
    const mwinEvent* up = Next(program, &at, mwin_eventPenUp);
    CHECK(down != nullptr && down->data.pen.position.x == 100.0f &&
              down->data.pen.pressure == 0.5f && down->data.pen.flags == mwin_penContact,
          "the Pencil down at half its force");
    CHECK(down != nullptr && fabsf(down->data.pen.tiltX - 45.0f) < 0.01f &&
              fabsf(down->data.pen.tiltY) < 0.01f,
          "tilted 45 degrees toward positive x");
    CHECK(moved != nullptr && moved->data.pen.position.y == 115.0f &&
              moved->data.pen.flags == mwin_penContact,
          "moved touching");
    CHECK(up != nullptr && up->data.pen.flags == 0, "lifted");
    at = 0;
    CHECK(Next(program, &at, mwin_eventPenButtonDown) == nullptr, "no button: the Pencil has none");
}

static bool PointerIs(const mwinEvent* event, mwinMouseButton button, uint8_t buttons,
                      uint8_t clicks)
{
    return event != nullptr && event->data.pointer.button == button &&
           event->data.pointer.buttons == buttons && event->data.pointer.clicks == clicks &&
           event->data.pointer.modifiers == mwin_modShift;
}

static void CheckPointer(const Program* program)
{
    int at = 0;
    const mwinEvent* left = Next(program, &at, mwin_eventButtonDown);
    const mwinEvent* right = Next(program, &at, mwin_eventButtonDown);
    CHECK(PointerIs(left, mwin_buttonLeft, 1, 2) && left->data.pointer.position.x == 200.0f,
          "a double click's left press with Shift");
    CHECK(PointerIs(right, mwin_buttonRight, 3, 2) && right->data.pointer.position.x == 220.0f,
          "the right pressed too, where the pointer moved");
    const mwinEvent* first = Next(program, &at, mwin_eventButtonUp);
    const mwinEvent* second = Next(program, &at, mwin_eventButtonUp);
    CHECK(PointerIs(first, mwin_buttonLeft, 2, 0) && PointerIs(second, mwin_buttonRight, 0, 0),
          "both released");
    at = 0;
    CHECK(Next(program, &at, mwin_eventCursorEntered) != nullptr &&
              Next(program, &at, mwin_eventCursorLeft) != nullptr,
          "the pointer hovering entered and left");
    at = 0;
    const mwinEvent* moved = nullptr;
    for (const mwinEvent* found = Next(program, &at, mwin_eventCursorMoved); found != nullptr;
         found = Next(program, &at, mwin_eventCursorMoved))
    {
        moved = found->data.pointer.position.x == 50.0f ? found : moved;
    }
    CHECK(moved != nullptr && moved->data.pointer.position.y == 60.0f, "hovering at its place");
    at = 0;
    const mwinEvent* wheel = Next(program, &at, mwin_eventWheel);
    CHECK(wheel != nullptr && wheel->data.wheel.x == 2.0f && wheel->data.wheel.y == 3.0f,
          "scrolling, ten points a detent, x turned");
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    UIView* view = ViewOf(context, program->window);
    if (program->phase == 0 && program->shown && view != nil)
    {
        printf("phase 0\n");
        @autoreleasepool
        {
            Fingers(program, view);
            Pencil(view);
            Pointer(view);
        }
        program->phase = 1;
        return mwin_frameContinue;
    }
    if (program->phase == 1)
    {
        printf("phase 1, %d records\n", program->count);
        CheckFingers(program);
        CheckPencil(program);
        CheckPointer(program);
        program->phase = 2;
        return mwin_frameStop;
    }
    if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d\n", program->phase);
        s_failures += 1;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase == 2, "every phase ran");
    printf("result: %d failures\n", s_failures);
}

int main(void)
{
    static Program program;
    // The runner names the file to write to (tools/run_ios_app.sh).
    const char* out = getenv("MWIN_TEST_OUT");
    if (out != nullptr && freopen(out, "w", stdout) == nullptr)
    {
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    // UIKit keeps the thread: this returns only if the program never ran.
    mwinResult result = mwinRun(&def);
    printf("result: mwinRun returned %d\n", (int)result);
    return 1;
}
