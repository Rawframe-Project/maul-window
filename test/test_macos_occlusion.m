// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Occlusion on macOS, read back from AppKit (a CI runner's session): a
// window an opaque window of the test's own covers wholly is occluded,
// revealed when the cover goes; minimized as the program asks, occluded,
// and revealed once it is windowed again. Each time the record, the
// window's state and AppKit's occlusionState agree.
//
// On a CI runner AppKit says no window is visible until user activity
// is declared, as when the display sleeps: the test first declares it,
// and brings its window to the front.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull
// How far the cover reaches past the window on every side, in points.
#define MARGIN 20.0

typedef enum Phase
{
    phaseCreate,
    phaseCovered,
    phaseUncovered,
    phaseMinimized,
    phaseRestored,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    NSWindow* host;
    NSWindow* cover;
    bool shown;
    // The occlusion notification the phase saw last, or 0.
    mwinEventType occlusion;
    bool timedOut;
    // Whether the display was woken and the window brought forward.
    bool woken;
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
        if (event.type == mwin_eventOccluded || event.type == mwin_eventRevealed)
        {
            program->occlusion = event.type;
        }
    }
}

// Whether AppKit says any part of the window is visible.
static bool Visible(const Program* program)
{
    return (program->host.occlusionState & NSWindowOcclusionStateVisible) != 0;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return program->shown && program->host != nil && Visible(program);
    case phaseCovered:
    case phaseMinimized:
        return program->occlusion == mwin_eventOccluded;
    case phaseUncovered:
    case phaseRestored:
        return program->occlusion == mwin_eventRevealed;
    default:
        return false;
    }
}

// An opaque window above the program's, over all of it.
static NSWindow* MakeCover(NSWindow* host)
{
    NSRect frame = NSInsetRect(host.frame, -MARGIN, -MARGIN);
    NSWindow* cover = [[NSWindow alloc] initWithContentRect:frame
                                                  styleMask:NSWindowStyleMaskBorderless
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
    cover.opaque = YES;
    cover.backgroundColor = NSColor.blackColor;
    cover.level = NSFloatingWindowLevel;
    cover.releasedWhenClosed = NO;
    [cover orderFrontRegardless];
    return cover;
}

// The window's state and AppKit agree with what the phase's record told.
static void CheckAgreed(const Program* program, mwinContext* context, bool occluded,
                        const char* what)
{
    mwinWindowState state;
    CHECK(mwinGetWindowState(context, program->window, &state) == mwin_success &&
              state.occluded == occluded && Visible(program) == !occluded,
          what);
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
        CheckAgreed(program, context, false, "a window nothing covers is not occluded");
        program->cover = MakeCover(program->host);
        break;
    case phaseCovered:
        CheckAgreed(program, context, true, "a window covered wholly is occluded");
        [program->cover orderOut:nil];
        break;
    case phaseUncovered:
        CheckAgreed(program, context, false, "a window uncovered is revealed");
        CHECK(mwinRequestMode(context, program->window, mwin_modeMinimized, nullptr) ==
                  mwin_success,
              "minimized");
        break;
    case phaseMinimized:
        CheckAgreed(program, context, true, "a minimized window is occluded");
        CHECK(mwinRequestMode(context, program->window, mwin_modeWindowed, nullptr) == mwin_success,
              "windowed");
        break;
    case phaseRestored:
        CheckAgreed(program, context, false, "a window windowed again is revealed");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->occlusion = (mwinEventType)0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS occlusion";
    def.titleLength = 20;
    def.size = (mwinSize){320.0f, 240.0f};
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

// Prints what AppKit and the window server say of the window, the
// display and the session, as a phase times out.
static void Tell(const Program* program)
{
    NSDictionary* session = (NSDictionary*)CGSessionCopyCurrentDictionary();
    printf("timed out in phase %d, last occlusion record %d: occlusion state %lu, visible %d, "
           "on active space %d, app active %d, display asleep %d, on console %s, screen locked "
           "%s, woken %d\n",
           (int)program->phase, (int)program->occlusion,
           program->host != nil ? (unsigned long)program->host.occlusionState : 0ul,
           program->host != nil && program->host.visible,
           program->host != nil && program->host.onActiveSpace, NSApp.active,
           (int)CGDisplayIsAsleep(CGMainDisplayID()),
           [[session[@"kCGSSessionOnConsoleKey"] description] UTF8String],
           [[session[@"CGSSessionScreenIsLocked"] description] UTF8String], program->woken);
    [session release];
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    mwinNativeHandles handles;
    if (program->host == nil &&
        mwinGetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        program->host = ((NSView*)handles.handles.apple.view).window;
    }
    if (program->host != nil && !program->woken)
    {
        IOPMAssertionID assertion = 0;
        (void)IOPMAssertionDeclareUserActivity(CFSTR("test_macos_occlusion"), kIOPMUserActiveLocal,
                                               &assertion);
        [NSApp activateIgnoringOtherApps:YES];
        [program->host makeKeyAndOrderFront:nil];
        program->woken = true;
    }
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
        Tell(program);
        return mwin_frameStop;
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    static Program program;
    setvbuf(stdout, nullptr, _IONBF, 0);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on macOS");
    CHECK(!program.timedOut, "every phase comes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    [program.cover release];
    return s_failures == 0 ? 0 : 1;
}
