// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The macOS backend's input against AppKit (a CI runner's session): key
// presses and releases by virtual key code with the layout's meaning,
// typed text, a modifier's press and release, mouse buttons with their
// click counts and a drag, and the wheel. No real input can be made
// without permissions, so the test makes NSEvents and hands them to the
// window as AppKit would; the wheel's go to the view.

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
} Record;

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
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
        if (event.type < mwin_eventKeyDown || event.type > mwin_eventWheel ||
            program->count == MAX_RECORDS)
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

static void SendKeys(NSWindow* window)
{
    [window sendEvent:Key(window, NSEventTypeKeyDown, 0, @"a", kVK_ANSI_A)];
    [window sendEvent:Key(window, NSEventTypeKeyUp, 0, @"a", kVK_ANSI_A)];
    // The left Shift's own bit is 0x2 in the device-dependent flags.
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, NSEventModifierFlagShift | 0x2, @"",
                          kVK_Shift)];
    [window sendEvent:Key(window, NSEventTypeFlagsChanged, 0, @"", kVK_Shift)];
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
        break;
    default:
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
    // The posted events come between frames, the release last.
    size_t at = 0;
    bool waiting = program->phase == 3 && program->keyWindow &&
                   Find(program, &at, mwin_eventKeyUp) == nullptr &&
                   NowNs() - program->startNs < DEADLINE_NS;
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
    return program->phase == 4 ? mwin_frameStop : mwin_frameContinue;
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
    CHECK(program.phase == 4, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
