// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Owned windows, popups and the icon on macOS, read back from AppKit (a
// CI runner's session): an owned window a child of its owner's; a menu
// borderless at its place against its owner's content, able to take the
// keyboard, and a tooltip that is not; a popup keeping its place as its
// owner moves and placed again against it; the application's icon made
// of the images and given back; and all of them gone with their owner.
// A menu that loses the keyboard is asked to close, which needs the
// program in front, as a runner's never is.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull

enum
{
    owner,
    dialog,
    menu,
    tooltip,
    windows,
};

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId ids[windows];
    mwinPosition moved[windows];
    int shown;
    int destroyed;
    int completed;
    mwinOutcome outcomes[2];
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static int IndexOf(const Program* program, mwinWindowId id)
{
    for (int i = 0; i < windows; i++)
    {
        if (program->ids[i].index1 == id.index1 && program->ids[i].generation == id.generation)
        {
            return i;
        }
    }
    return -1;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        int index = IndexOf(program, event.window);
        program->shown += event.type == mwin_eventShown;
        program->destroyed += event.type == mwin_eventWindowDestroyed;
        if (event.type == mwin_eventMoved && index >= 0)
        {
            program->moved[index] = event.data.position;
        }
        if (event.type == mwin_eventRequestCompleted &&
            event.data.completion.kind == mwin_requestIcon && program->completed < 2)
        {
            program->outcomes[program->completed++] = event.data.completion.outcome;
        }
    }
}

static NSWindow* WindowOf(mwinContext* context, mwinWindowId id)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, id, &handles) == mwin_success
               ? ((NSView*)handles.handles.apple.view).window
               : nil;
}

// The top left of a window's content from the primary screen's top left.
static NSPoint TopLeftOf(NSWindow* window)
{
    NSRect content = [window contentRectForFrameRect:window.frame];
    return NSMakePoint(NSMinX(content), NSScreen.screens[0].frame.size.height - NSMaxY(content));
}

static bool At(NSWindow* popup, NSWindow* parent, CGFloat x, CGFloat y)
{
    NSPoint a = TopLeftOf(popup);
    NSPoint b = TopLeftOf(parent);
    return a.x - b.x == x && a.y - b.y == y;
}

static mwinResult Make(Program* program, mwinContext* context, int which)
{
    static const char* const titles[] = {"Owner", "Dialog", "Menu", "Tooltip"};
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = titles[which];
    def.titleLength = strlen(titles[which]);
    def.size = which >= menu ? (mwinSize){120.0f, 80.0f} : (mwinSize){320.0f, 240.0f};
    def.owner = which == owner ? (mwinWindowId){0} : program->ids[owner];
    def.kind = which == menu      ? mwin_windowMenu
               : which == tooltip ? mwin_windowTooltip
                                  : mwin_windowNormal;
    def.position = which == menu ? (mwinPosition){30.0f, 40.0f} : (mwinPosition){10.0f, 250.0f};
    return mwinCreateWindow(context, &def, &program->ids[which], nullptr);
}

static void CheckMade(const Program* program, mwinContext* context)
{
    NSWindow* parent = WindowOf(context, program->ids[owner]);
    NSWindow* owned = WindowOf(context, program->ids[dialog]);
    NSWindow* popup = WindowOf(context, program->ids[menu]);
    NSWindow* tip = WindowOf(context, program->ids[tooltip]);
    CHECK(owned.parentWindow == parent && popup.parentWindow == parent &&
              tip.parentWindow == parent,
          "each a child of its owner's window");
    CHECK((popup.styleMask & NSWindowStyleMaskTitled) == 0 && popup.canBecomeKeyWindow &&
              !tip.canBecomeKeyWindow && owned.canBecomeKeyWindow,
          "popups borderless, a menu able to take the keyboard, a tooltip not");
    CHECK(At(popup, parent, 30.0, 40.0) && program->moved[menu].x == 30.0f &&
              program->moved[menu].y == 40.0f,
          "a menu at its place against its owner's content, as reported");
}

static void Icon(mwinContext* context, mwinWindowId window)
{
    static uint8_t small[16 * 16 * 4];
    static uint8_t large[32 * 32 * 4];
    memset(small, 0xFF, sizeof(small));
    memset(large, 0x80, sizeof(large));
    const mwinIconImage images[] = {{16, 16, 16 * 4, small}, {32, 32, 32 * 4, large}};
    CHECK(mwinRequestIcon(context, window, images, 2, nullptr) == mwin_success, "an icon");
}

// The middle pixel of an image drawn at 32 by 32, premultiplied RGBA.
static void MiddleOf(NSImage* icon, uint8_t pixel[4])
{
    uint8_t pixels[32 * 32 * 4] = {0};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef bitmap = CGBitmapContextCreate(pixels, 32, 32, 8, 32 * 4, space,
                                                (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGImageRef image = [icon CGImageForProposedRect:nullptr context:nil hints:nil];
    CGContextDrawImage(bitmap, CGRectMake(0, 0, 32, 32), image);
    memcpy(pixel, &pixels[(16 * 32 + 16) * 4], 4);
    CGContextRelease(bitmap);
    CGColorSpaceRelease(space);
}

static bool Near(uint8_t value, int expected)
{
    return value >= expected - 12 && value <= expected + 12;
}

// AppKit keeps a snapshot of the icon at its size, drawn from the image
// nearest its pixels: the large one, grey at half alpha, not the small
// one, white and opaque.
static void CheckIcon(void)
{
    NSImage* icon = NSApp.applicationIconImage;
    uint8_t pixel[4];
    MiddleOf(icon, pixel);
    CHECK(icon.size.width == 32.0 && icon.size.height == 32.0,
          "the application's icon at the largest's size");
    CHECK(Near(pixel[0], 0x40) && Near(pixel[3], 0x80), "drawn from the image nearest its pixels");
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->shown >= 1;
    case 1:
        return program->shown >= 4;
    case 3:
        return program->completed >= 1;
    case 4:
        return program->completed >= 2;
    case 5:
        return program->destroyed >= windows;
    default:
        return true;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    NSWindow* parent = WindowOf(context, program->ids[owner]);
    switch (program->phase)
    {
    case 0:
        CHECK(Make(program, context, dialog) == mwin_success &&
                  Make(program, context, menu) == mwin_success &&
                  Make(program, context, tooltip) == mwin_success,
              "an owned window, a menu and a tooltip");
        break;
    case 1:
        CheckMade(program, context);
        CHECK(mwinRequestPosition(context, program->ids[owner], (mwinPosition){200.0f, 150.0f},
                                  nullptr) == mwin_success &&
                  mwinRequestPosition(context, program->ids[tooltip], (mwinPosition){50.0f, 60.0f},
                                      nullptr) == mwin_success,
              "the owner moved, the tooltip placed");
        break;
    case 2:
        CHECK(At(WindowOf(context, program->ids[menu]), parent, 30.0, 40.0) &&
                  program->moved[menu].x == 30.0f,
              "the menu keeps its place against its owner");
        CHECK(At(WindowOf(context, program->ids[tooltip]), parent, 50.0, 60.0) &&
                  program->moved[tooltip].x == 50.0f && program->moved[tooltip].y == 60.0f,
              "the tooltip placed against it");
        Icon(context, program->ids[owner]);
        break;
    case 3:
        CHECK(program->outcomes[0] == mwin_outcomeDone, "the icon set");
        CheckIcon();
        CHECK(mwinRequestIcon(context, program->ids[dialog], nullptr, 0, nullptr) == mwin_success,
              "no icon");
        break;
    case 4:
        CHECK(program->outcomes[1] == mwin_outcomeDone &&
                  NSApp.applicationIconImage.size.width != 32.0,
              "the application's own icon again");
        CHECK(mwinDestroyWindow(context, program->ids[owner]) == mwin_success, "the owner gone");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    return Make(program, context, owner);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (Ready(program))
    {
        @autoreleasepool
        {
            Advance(program, context);
        }
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        printf("timed out in phase %d\n", program->phase);
        return mwin_frameStop;
    }
    return program->phase == 6 ? mwin_frameStop : mwin_frameContinue;
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
    CHECK(!program.timedOut, "every phase comes in time");
    CHECK(program.phase == 6, "every phase ran, all four windows gone with the owner");
    return s_failures == 0 ? 0 : 1;
}
