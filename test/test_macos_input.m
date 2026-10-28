// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The macOS backend's input against AppKit (a CI runner's session): key
// presses and releases by virtual key code with the layout's meaning,
// typed text, each modifier's press and release and Caps Lock toggled,
// mouse buttons with their click counts and a drag, the right, middle,
// back and forward buttons, the wheel, and the cursor requests, with a
// cursor made from images that a shape replaces and that leaves the
// arrow when destroyed, then each other shape as AppKit's own cursor.
// No real input can be made without permissions, so the test makes
// NSEvents and hands them to the window as AppKit would; the wheel's and
// the buttons that only a Quartz event numbers go to the view. A cursor
// captured waits for the window to be the key window, which a CI
// runner's session never makes it, so motion as raw deltas is not
// exercised.

#include "cursor_images.h"
#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull
#define MAX_RECORDS 64

typedef struct Record
{
    mwinEventType type;
    mwinKeyEvent key;
    mwinPointerEvent pointer;
    mwinWheelEvent wheel;
    char text[16];
    mwinOutcome outcome;
} Record;

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinCursorId cursor;
    bool shown;
    Record records[MAX_RECORDS];
    size_t count;
    bool keyWindow;
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
        bool input = event.type >= mwin_eventKeyDown && event.type <= mwin_eventWheel;
        if ((!input && event.type != mwin_eventRequestCompleted) || program->count == MAX_RECORDS)
        {
            continue;
        }
        Record* record = &program->records[program->count++];
        *record = (Record){.type = event.type};
        if (event.type == mwin_eventKeyDown || event.type == mwin_eventKeyUp)
        {
            record->key = event.data.key;
        }
        else if (event.type == mwin_eventTextInput)
        {
            size_t length = event.data.text.length < 15 ? event.data.text.length : 15;
            memcpy(record->text, event.data.text.text, length);
        }
        else if (event.type == mwin_eventRequestCompleted)
        {
            record->outcome = event.data.completion.outcome;
        }
        else if (event.type == mwin_eventWheel)
        {
            record->wheel = event.data.wheel;
        }
        else
        {
            record->pointer = event.data.pointer;
        }
    }
}

// The next record of a type from a place in the list, or nullptr.
static const Record* Find(const Program* program, size_t* at, mwinEventType type)
{
    for (; *at < program->count; (*at)++)
    {
        if (program->records[*at].type == type)
        {
            return &program->records[(*at)++];
        }
    }
    return nullptr;
}

static NSWindow* WindowOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? ((NSView*)handles.handles.apple.view).window
               : nil;
}

static NSEvent* Key(NSWindow* window, NSEventType type, NSEventModifierFlags flags,
                    NSString* characters, unsigned short code)
{
    return [NSEvent keyEventWithType:type
                            location:NSZeroPoint
                       modifierFlags:flags
                           timestamp:0.0
                        windowNumber:window.windowNumber
                             context:nil
                          characters:characters
         charactersIgnoringModifiers:characters
                           isARepeat:NO
                             keyCode:code];
}

// A mouse event at a place of the content, from its top left.
static NSEvent* Mouse(NSWindow* window, NSEventType type, NSPoint place, NSInteger clicks)
{
    NSView* view = window.contentView;
    return [NSEvent mouseEventWithType:type
                              location:[view convertPoint:place toView:nil]
                         modifierFlags:0
                             timestamp:0.0
                          windowNumber:window.windowNumber
                               context:nil
                           eventNumber:0
                            clickCount:clicks
                              pressure:type == NSEventTypeLeftMouseUp ? 0.0f : 1.0f];
}

// A modifier key: its virtual key, its class's flag and its own bit in
// the device-dependent flags, and its code and class.
typedef struct Modifier
{
    unsigned short virtualKey;
    NSEventModifierFlags flags;
    NSEventModifierFlags bit;
    mwinKeyCode code;
    mwinModifiers modifier;
} Modifier;

static const Modifier s_modifiers[] = {
    {kVK_RightShift, NSEventModifierFlagShift, 0x4, mwin_codeShiftRight, mwin_modShift},
    {kVK_Control, NSEventModifierFlagControl, 0x1, mwin_codeControlLeft, mwin_modControl},
    {kVK_RightControl, NSEventModifierFlagControl, 0x2000, mwin_codeControlRight, mwin_modControl},
    {kVK_Command, NSEventModifierFlagCommand, 0x8, mwin_codeMetaLeft, mwin_modMeta},
    {kVK_RightCommand, NSEventModifierFlagCommand, 0x10, mwin_codeMetaRight, mwin_modMeta},
    {kVK_Option, NSEventModifierFlagOption, 0x20, mwin_codeAltLeft, mwin_modAlt},
    {kVK_RightOption, NSEventModifierFlagOption, 0x40, mwin_codeAltRight, mwin_modAlt},
};

static void SendKeys(NSWindow* window)
{
    [window sendEvent:Key(window, NSEventTypeKeyDown, 0, @"a", kVK_ANSI_A)];
    [window sendEvent:Key(window, NSEventTypeKeyUp, 0, @"a", kVK_ANSI_A)];
    // The left Shift's own bit is 0x2 in the device-dependent flags.
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, NSEventModifierFlagShift | 0x2, @"",
                          kVK_Shift)];
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, 0, @"", kVK_Shift)];
    // Each other modifier, down with its own bit and up.
    for (size_t i = 0; i < sizeof(s_modifiers) / sizeof(s_modifiers[0]); i++)
    {
        const Modifier* modifier = &s_modifiers[i];
        [window sendEvent:Key(window, NSEventTypeFlagsChanged, modifier->flags | modifier->bit, @"",
                              modifier->virtualKey)];
        [window sendEvent:Key(window, NSEventTypeFlagsChanged, 0, @"", modifier->virtualKey)];
    }
    // Caps Lock toggled on: macOS tells nothing more.
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, NSEventModifierFlagCapsLock, @"",
                          kVK_CapsLock)];
}

static void SendMouse(NSWindow* window)
{
    [window sendEvent:Mouse(window, NSEventTypeLeftMouseDown, NSMakePoint(50.0, 40.0), 1)];
    [window sendEvent:Mouse(window, NSEventTypeLeftMouseDragged, NSMakePoint(60.0, 45.0), 1)];
    [window sendEvent:Mouse(window, NSEventTypeLeftMouseUp, NSMakePoint(60.0, 45.0), 1)];
    [window sendEvent:Mouse(window, NSEventTypeLeftMouseDown, NSMakePoint(60.0, 45.0), 2)];
    [window sendEvent:Mouse(window, NSEventTypeLeftMouseUp, NSMakePoint(60.0, 45.0), 2)];
    // Three lines away from the user and two to the left.
    CGEventRef wheel = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 2, 3, 2);
    [window.contentView scrollWheel:[NSEvent eventWithCGEvent:wheel]];
    CFRelease(wheel);
    // The right button through the window; the middle, back and forward
    // buttons, whose number only a Quartz event carries, to the view.
    [window sendEvent:Mouse(window, NSEventTypeRightMouseDown, NSMakePoint(60.0, 45.0), 1)];
    [window sendEvent:Mouse(window, NSEventTypeRightMouseUp, NSMakePoint(60.0, 45.0), 1)];
    for (int64_t number = 2; number <= 4; number++)
    {
        for (int up = 0; up < 2; up++)
        {
            CGEventRef other =
                CGEventCreateMouseEvent(nullptr, up ? kCGEventOtherMouseUp : kCGEventOtherMouseDown,
                                        CGPointZero, kCGMouseButtonCenter);
            CGEventSetIntegerValueField(other, kCGMouseEventButtonNumber, number);
            NSEvent* event = [NSEvent eventWithCGEvent:other];
            if (up)
            {
                [window.contentView otherMouseUp:event];
            }
            else
            {
                [window.contentView otherMouseDown:event];
            }
            CFRelease(other);
        }
    }
}

// Command and a key through the application's queue, where AppKit
// keeps the release from the window and the backend's event monitor
// hands it on: only when the window is the key window, which a CI
// runner's session never makes it.
static void SendCommand(NSWindow* window)
{
    [NSApp postEvent:Key(window, NSEventTypeKeyDown, NSEventModifierFlagCommand, @"a", kVK_ANSI_A)
             atStart:NO];
    [NSApp postEvent:Key(window, NSEventTypeKeyUp, NSEventModifierFlagCommand, @"a", kVK_ANSI_A)
             atStart:NO];
}

static void CheckKeys(const Program* program, mwinContext* context)
{
    size_t at = 0;
    mwinKey meaning = mwinMapKeyCode(context, mwin_codeKeyA);
    CHECK(meaning != 0 && (meaning & MWIN_KEY_NAMED) == 0, "the layout gives the A key a letter");
    const Record* down = Find(program, &at, mwin_eventKeyDown);
    CHECK(down != nullptr && down->key.code == mwin_codeKeyA && down->key.key == meaning &&
              down->key.modifiers == 0 && !down->key.repeat,
          "a key press by its code, with the layout's meaning");
    size_t textAt = 0;
    const Record* text = Find(program, &textAt, mwin_eventTextInput);
    CHECK(text != nullptr && strcmp(text->text, "a") == 0, "the key types its text");
    const Record* up = Find(program, &at, mwin_eventKeyUp);
    CHECK(up != nullptr && up->key.code == mwin_codeKeyA, "its release");
    const Record* shift = Find(program, &at, mwin_eventKeyDown);
    CHECK(shift != nullptr && shift->key.code == mwin_codeShiftLeft &&
              shift->key.modifiers == mwin_modShift,
          "the left Shift's press, with Shift held");
    const Record* unshift = Find(program, &at, mwin_eventKeyUp);
    CHECK(unshift != nullptr && unshift->key.code == mwin_codeShiftLeft &&
              unshift->key.modifiers == 0,
          "and its release");
    for (size_t i = 0; i < sizeof(s_modifiers) / sizeof(s_modifiers[0]); i++)
    {
        const Modifier* modifier = &s_modifiers[i];
        const Record* pressed = Find(program, &at, mwin_eventKeyDown);
        const Record* released = Find(program, &at, mwin_eventKeyUp);
        CHECK(pressed != nullptr && pressed->key.code == modifier->code &&
                  pressed->key.modifiers == modifier->modifier && released != nullptr &&
                  released->key.code == modifier->code && released->key.modifiers == 0,
              "each other modifier: its press with its class held, and its release");
    }
    const Record* caps = Find(program, &at, mwin_eventKeyDown);
    const Record* capsUp = Find(program, &at, mwin_eventKeyUp);
    CHECK(caps != nullptr && caps->key.code == mwin_codeCapsLock &&
              caps->key.modifiers == mwin_modCapsLock && capsUp != nullptr &&
              capsUp->key.code == mwin_codeCapsLock,
          "Caps Lock toggled: a press and a release at once");
    char name[64];
    size_t length = 0;
    CHECK(mwinGetKeyboardLayout(context, name, sizeof(name), &length) == mwin_success && length > 0,
          "the layout has a name");
}

static void CheckMouse(const Program* program)
{
    size_t at = 0;
    const Record* down = Find(program, &at, mwin_eventButtonDown);
    CHECK(down != nullptr && down->pointer.button == mwin_buttonLeft && down->pointer.clicks == 1 &&
              down->pointer.buttons == 1 && down->pointer.position.x == 50.0f &&
              down->pointer.position.y == 40.0f,
          "a press at its place, from the top left");
    const Record* moved = Find(program, &at, mwin_eventCursorMoved);
    CHECK(moved != nullptr && moved->pointer.buttons == 1 && moved->pointer.position.x == 60.0f &&
              moved->pointer.position.y == 45.0f,
          "a drag with the button held");
    const Record* up = Find(program, &at, mwin_eventButtonUp);
    CHECK(up != nullptr && up->pointer.buttons == 0, "the release");
    const Record* second = Find(program, &at, mwin_eventButtonDown);
    CHECK(second != nullptr && second->pointer.clicks == 2, "a double click's second press");
    size_t wheelAt = 0;
    const Record* wheel = Find(program, &wheelAt, mwin_eventWheel);
    CHECK(wheel != nullptr && wheel->wheel.y == 3.0f && wheel->wheel.x == -2.0f,
          "a wheel's lines as detents, away from the user and to the left");
    static const mwinMouseButton others[] = {mwin_buttonRight, mwin_buttonMiddle, mwin_buttonBack,
                                             mwin_buttonForward};
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); i++)
    {
        const Record* pressed = Find(program, &at, mwin_eventButtonDown);
        const Record* released = Find(program, &at, mwin_eventButtonUp);
        CHECK(pressed != nullptr && pressed->pointer.button == others[i] && released != nullptr &&
                  released->pointer.button == others[i],
              "the right, middle, back and forward buttons, each pressed and released");
    }
}

static void CheckCommand(const Program* program)
{
    size_t at = 0;
    const Record* down = Find(program, &at, mwin_eventKeyDown);
    const Record* up = Find(program, &at, mwin_eventKeyUp);
    size_t textAt = 0;
    CHECK(down != nullptr && down->key.modifiers == mwin_modMeta && up != nullptr &&
              Find(program, &textAt, mwin_eventTextInput) == nullptr,
          "Command and a key: its press and release, and no text");
}

// The cursor requests: a shape, and each mode but confinement, which
// macOS cannot do. A captured cursor waits for the window to be the key
// window, which a CI runner's session never makes it.
static void RequestCursors(mwinContext* context, mwinWindowId window)
{
    CHECK(
        mwinRequestCursorShape(context, window, mwin_shapeText, nullptr) == mwin_success &&
            mwinRequestCursorMode(context, window, mwin_cursorConfined, nullptr) == mwin_success &&
            mwinRequestCursorMode(context, window, mwin_cursorHidden, nullptr) == mwin_success &&
            mwinRequestCursorMode(context, window, mwin_cursorCaptured, nullptr) == mwin_success &&
            mwinRequestCursorMode(context, window, mwin_cursorVisible, nullptr) == mwin_success,
        "the cursor requests");
}

static void CheckCursors(const Program* program)
{
    static const mwinOutcome expected[] = {mwin_outcomeDone, mwin_outcomeUnsupported,
                                           mwin_outcomeDone, mwin_outcomeDone, mwin_outcomeDone};
    size_t at = 0;
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    {
        const Record* completed = Find(program, &at, mwin_eventRequestCompleted);
        CHECK(completed != nullptr && completed->outcome == expected[i],
              "a shape, no confinement, hiding, capturing and showing");
    }
}

// Whether the application's cursor is the one made from images: the
// first image's size in points, the hot spot, and a representation of
// each image. The drag before left the pointer over the view.
static bool ShowsImages(void)
{
    NSCursor* cursor = [NSCursor currentCursor];
    return NSEqualSizes(cursor.image.size, NSMakeSize(16.0, 16.0)) &&
           NSEqualPoints(cursor.hotSpot, NSMakePoint(3.0, 5.0)) &&
           cursor.image.representations.count == 2;
}

static const Record* Completed(const Program* program)
{
    size_t at = 0;
    return Find(program, &at, mwin_eventRequestCompleted);
}

// The shapes besides the text one, in the order the phases ask for them.
#define SHAPES     8
#define LAST_PHASE (8 + SHAPES)
static const mwinCursorShape s_shapes[SHAPES] = {mwin_shapePointer,
                                                 mwin_shapeCrosshair,
                                                 mwin_shapeMove,
                                                 mwin_shapeResizeEastWest,
                                                 mwin_shapeResizeNorthSouth,
                                                 mwin_shapeResizeNortheastSouthwest,
                                                 mwin_shapeResizeNorthwestSoutheast,
                                                 mwin_shapeNotAllowed};

// The cursor made from images: shown, replaced by a shape, shown again,
// and destroyed.
static void AdvanceImages(Program* program, mwinContext* context)
{
    const Record* completed = Completed(program);
    bool done = completed != nullptr && completed->outcome == mwin_outcomeDone;
    switch (program->phase)
    {
    case 5:
        CHECK(done && ShowsImages(), "the cursor made from images over the view");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a shape again");
        break;
    case 6:
        CHECK(done && [NSCursor currentCursor] == [NSCursor IBeamCursor],
              "the shape in the images' place");
        CHECK(mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                  mwin_success,
              "the images again");
        break;
    default:
        CHECK(done && ShowsImages(), "the images in the shape's place");
        CHECK(mwinDestroyCursor(context, program->cursor) == mwin_success &&
                  [NSCursor currentCursor] == [NSCursor arrowCursor],
              "destroyed: the arrow");
        CHECK(mwinRequestCursorShape(context, program->window, s_shapes[0], nullptr) ==
                  mwin_success,
              "each other shape");
        break;
    }
}

// AppKit's cursor for a shape: a diagonal resize cursor of its own from
// macOS 15, the arrow before it.
static NSCursor* AppKitCursor(mwinCursorShape shape)
{
    switch (shape)
    {
    case mwin_shapePointer:
        return [NSCursor pointingHandCursor];
    case mwin_shapeCrosshair:
        return [NSCursor crosshairCursor];
    case mwin_shapeMove:
        return [NSCursor openHandCursor];
    case mwin_shapeResizeEastWest:
        return [NSCursor resizeLeftRightCursor];
    case mwin_shapeResizeNorthSouth:
        return [NSCursor resizeUpDownCursor];
    case mwin_shapeResizeNortheastSouthwest:
        if (@available(macOS 15.0, *))
        {
            return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                                              inDirections:NSCursorFrameResizeDirectionsAll];
        }
        return [NSCursor arrowCursor];
    case mwin_shapeResizeNorthwestSoutheast:
        if (@available(macOS 15.0, *))
        {
            return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                                              inDirections:NSCursorFrameResizeDirectionsAll];
        }
        return [NSCursor arrowCursor];
    default:
        return [NSCursor operationNotAllowedCursor];
    }
}

// Whether two cursors look the same: their images and hot spots, since
// AppKit may make a cursor anew each time it is asked for.
static bool SameCursor(NSCursor* a, NSCursor* b)
{
    return [a.image.TIFFRepresentation isEqual:b.image.TIFFRepresentation] &&
           NSEqualPoints(a.hotSpot, b.hotSpot);
}

// Each shape the earlier phases did not show, one a phase: the shape
// the last one asked for shows, and the next is asked for.
static void AdvanceShapes(Program* program, mwinContext* context)
{
    size_t index = (size_t)program->phase - 8;
    const Record* completed = Completed(program);
    CHECK(completed != nullptr && completed->outcome == mwin_outcomeDone &&
              SameCursor([NSCursor currentCursor], AppKitCursor(s_shapes[index])),
          "pointer, crosshair, move, the four resizes and not allowed, each AppKit's cursor");
    if (index + 1 < SHAPES)
    {
        CHECK(mwinRequestCursorShape(context, program->window, s_shapes[index + 1], nullptr) ==
                  mwin_success,
              "the next shape");
    }
}

// Each phase sends its events at once, and the next frame reads what
// they posted.
static void Advance(Program* program, mwinContext* context)
{
    NSWindow* window = WindowOf(context, program->window);
    switch (program->phase)
    {
    case 0:
        CHECK(window != nil, "the window is made");
        SendKeys(window);
        break;
    case 1:
        CheckKeys(program, context);
        SendMouse(window);
        break;
    case 2:
        CheckMouse(program);
        program->keyWindow = window.keyWindow;
        if (program->keyWindow)
        {
            SendCommand(window);
        }
        else
        {
            printf("not the key window: Command's release not tried\n");
        }
        break;
    case 3:
        if (program->keyWindow)
        {
            CheckCommand(program);
        }
        RequestCursors(context, program->window);
        break;
    case 4:
    {
        CheckCursors(program);
        mwinIconImage images[2];
        mwinCursorDef def = CursorImagesDef(images);
        CHECK(mwinCreateCursor(context, &def, &program->cursor) == mwin_success &&
                  mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                      mwin_success,
              "a cursor made from images");
        break;
    }
    case 5:
    case 6:
    case 7:
        AdvanceImages(program, context);
        break;
    default:
        AdvanceShapes(program, context);
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS input";
    def.titleLength = 16;
    def.size = (mwinSize){320.0f, 240.0f};
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    // The posted events come between frames, the release last; the
    // cursor requests complete as they are carried out.
    size_t at = 0;
    size_t completions = 0;
    for (size_t i = 0; i < program->count; i++)
    {
        completions += program->records[i].type == mwin_eventRequestCompleted ? 1 : 0;
    }
    bool pending = (program->phase == 3 && program->keyWindow &&
                    Find(program, &at, mwin_eventKeyUp) == nullptr) ||
                   (program->phase == 4 && completions < 5) ||
                   (program->phase > 4 && completions < 1);
    bool waiting = pending && NowNs() - program->startNs < DEADLINE_NS;
    if (program->shown && !waiting)
    {
        @autoreleasepool
        {
            Advance(program, context);
        }
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == LAST_PHASE ? mwin_frameStop : mwin_frameContinue;
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
    CHECK(program.phase == LAST_PHASE, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
