// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Window chrome on macOS, read back from AppKit (a CI runner's
// session): custom chrome, its content the whole frame, the title bar
// clear and its buttons hidden; opacity; an aspect ratio set and lifted;
// size limits, the window brought into them; the hit regions: a press
// on the client and on a close button the program's, a press on an
// edge resizing the window and not reported, a double click on a caption
// zooming it; then undecorated and always on top. A single press on a
// caption moves the window through the window server, which the test
// leaves alone.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    NSWindow* host;
    bool shown;
    int completions;
    mwinOutcome outcomes[4];
    int downs;
    int ups;
    mwinSize size;
    mwinWindowMode mode;
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
        program->downs += event.type == mwin_eventButtonDown;
        program->ups += event.type == mwin_eventButtonUp;
        program->size = event.type == mwin_eventResized ? event.data.size : program->size;
        program->mode = event.type == mwin_eventModeChanged ? event.data.mode : program->mode;
        if (event.type == mwin_eventRequestCompleted && program->completions < 4)
        {
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
    }
}

// A mouse event at a place of the content, from its top left.
static void Mouse(NSWindow* window, NSEventType type, CGFloat x, CGFloat y, NSInteger clicks)
{
    NSView* view = window.contentView;
    [window sendEvent:[NSEvent mouseEventWithType:type
                                         location:[view convertPoint:NSMakePoint(x, y) toView:nil]
                                    modifierFlags:0
                                        timestamp:0.0
                                     windowNumber:window.windowNumber
                                          context:nil
                                      eventNumber:0
                                       clickCount:clicks
                                         pressure:1.0f]];
}

static bool Done(const Program* program, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (program->outcomes[i] != mwin_outcomeDone)
        {
            return false;
        }
    }
    return program->completions == count;
}

static void CheckCustom(const Program* program)
{
    NSWindow* window = program->host;
    NSSize frame = window.frame.size;
    CHECK(Done(program, 3), "custom chrome, opacity and a ratio done");
    CHECK((window.styleMask & NSWindowStyleMaskFullSizeContentView) != 0 &&
              window.titlebarAppearsTransparent &&
              [window standardWindowButton:NSWindowCloseButton].hidden,
          "custom chrome: the content fills the frame, the title bar clear, its buttons hidden");
    CHECK(program->size.width == (float)frame.width && program->size.height == (float)frame.height,
          "the content reported as the whole frame");
    CHECK(window.alphaValue == 0.5, "half opaque");
    CHECK(NSEqualSizes(window.contentAspectRatio, NSMakeSize(16.0, 9.0)), "a ratio of 16 to 9");
}

static void CheckLimits(const Program* program)
{
    NSWindow* window = program->host;
    CHECK(Done(program, 2), "the ratio lifted and limits set");
    CHECK(window.contentAspectRatio.width == 0.0, "no ratio");
    CHECK(NSEqualSizes(window.contentMinSize, NSMakeSize(200.0, 150.0)) &&
              NSEqualSizes(window.contentMaxSize, NSMakeSize(250.0, 200.0)),
          "the limits");
    CHECK(program->size.width == 250.0f && program->size.height == 200.0f,
          "the window brought into them");
}

static void Press(NSWindow* window)
{
    Mouse(window, NSEventTypeLeftMouseDown, 100.0, 100.0, 1);
    Mouse(window, NSEventTypeLeftMouseUp, 100.0, 100.0, 1);
    Mouse(window, NSEventTypeLeftMouseDown, 230.0, 15.0, 1);
    Mouse(window, NSEventTypeLeftMouseUp, 230.0, 15.0, 1);
    // A bottom right corner region, dragged 30 right and 20 down. It lies
    // inside the content: near the frame's own corner AppKit resizes the
    // window itself.
    Mouse(window, NSEventTypeLeftMouseDown, 190.0, 140.0, 1);
    Mouse(window, NSEventTypeLeftMouseDragged, 220.0, 160.0, 1);
    Mouse(window, NSEventTypeLeftMouseUp, 220.0, 160.0, 1);
}

static bool Ready(const Program* program)
{
    static const int answers[] = {0, 3, 2, 2, 0, 0, 1};
    if (program->phase == 5)
    {
        return program->mode == mwin_modeMaximized;
    }
    return program->phase == 0 ? program->shown
                               : program->completions >= answers[program->phase % 7];
}

static void Advance(Program* program, mwinContext* context)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case 0:
        CHECK(mwinRequestStyle(context, window,
                               mwin_styleDecorated | mwin_styleResizable | mwin_styleCustomChrome,
                               nullptr) == mwin_success &&
                  mwinRequestOpacity(context, window, 0.5f, nullptr) == mwin_success &&
                  mwinRequestAspectRatio(context, window, 16, 9, nullptr) == mwin_success,
              "custom chrome, half opaque, 16 to 9");
        break;
    case 1:
        CheckCustom(program);
        CHECK(mwinRequestAspectRatio(context, window, 0, 0, nullptr) == mwin_success &&
                  mwinRequestSizeLimits(context, window, (mwinSize){200.0f, 150.0f},
                                        (mwinSize){250.0f, 200.0f}, nullptr) == mwin_success,
              "no ratio, limits");
        break;
    case 2:
    {
        CheckLimits(program);
        static const mwinHitRegion regions[] = {
            {{0.0f, 0.0f, 250.0f, 30.0f}, mwin_hitCaption},
            {{220.0f, 5.0f, 20.0f, 20.0f}, mwin_hitClose},
            {{180.0f, 130.0f, 20.0f, 20.0f}, mwin_hitBottomRight}};
        CHECK(mwinRequestSizeLimits(context, window, (mwinSize){0}, (mwinSize){0}, nullptr) ==
                      mwin_success &&
                  mwinRequestHitRegions(context, window, regions, 3, nullptr) == mwin_success,
              "free again, hit regions");
        break;
    }
    case 3:
        Press(program->host);
        break;
    case 4:
        CHECK(program->downs == 2 && program->ups == 2,
              "presses on the client and a close button the program's, the edge's not");
        CHECK(program->size.width == 280.0f && program->size.height == 220.0f,
              "the edge resizes the window");
        Mouse(program->host, NSEventTypeLeftMouseDown, 100.0, 15.0, 2);
        break;
    case 5:
        CHECK(program->downs == 2, "a double click on a caption zooms, and is not reported");
        CHECK(mwinRequestStyle(context, window, mwin_styleResizable | mwin_styleAlwaysOnTop,
                               nullptr) == mwin_success,
              "undecorated, always on top");
        break;
    default:
        CHECK(Done(program, 1) && (program->host.styleMask & NSWindowStyleMaskTitled) == 0 &&
                  program->host.level == NSFloatingWindowLevel,
              "borderless and floating");
        break;
    }
    program->phase += 1;
    program->completions = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS chrome";
    def.titleLength = 17;
    def.size = (mwinSize){320.0f, 240.0f};
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
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
    return program->phase == 7 ? mwin_frameStop : mwin_frameContinue;
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
    CHECK(program.phase == 7, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
