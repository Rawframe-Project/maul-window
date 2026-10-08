// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend against UIKit, in the simulator (tools/run_ios_app.sh):
// a window made on the launch scene, its CAMetalLayer view, its size,
// scale, safe area and mode as UIKit lays it out, the screen as the
// primary monitor; the title as the scene's, a size request
// unsupported, hiding and showing; the application going to the
// background and back, as UIKit tells the scene's delegate, suspending
// told in a frame of its own and no frames while suspended; a destroyed window's
// scene taken by the next window. mwinRun never returns on iOS: quit
// prints the closing line the runner waits for.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/native.h"

#import <UIKit/UIKit.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    int frames;
    bool shown;
    bool hidden;
    int completions;
    mwinOutcome outcomes[4];
    // The lifecycle records in order, with the frame each came in and
    // when that frame ran.
    int lifecycle;
    mwinEventType lifecycleTypes[4];
    int lifecycleFrames[4];
    uint64_t lifecycleNs[4];
    UIWindowScene* scene;
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
        program->hidden |= event.type == mwin_eventHidden;
        if (event.type == mwin_eventRequestCompleted && program->completions < 4)
        {
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
        if (event.type >= mwin_eventSuspending && event.type <= mwin_eventResumed &&
            program->lifecycle < 4)
        {
            program->lifecycleTypes[program->lifecycle] = event.type;
            program->lifecycleFrames[program->lifecycle] = program->frames;
            program->lifecycleNs[program->lifecycle] = NowNs();
            program->lifecycle += 1;
        }
    }
}

static UIView* ViewOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    if (mwinGetNativeHandles(context, window, &handles) != mwin_success ||
        handles.platform != mwin_platformIOS)
    {
        return nil;
    }
    return (UIView*)handles.handles.apple.view;
}

// UIKit laid the window out and the program heard all of it.
static bool LaidOut(mwinContext* context, mwinWindowId window)
{
    UIView* view = ViewOf(context, window);
    mwinWindowState state;
    if (view == nil || mwinGetWindowState(context, window, &state) != mwin_success)
    {
        return false;
    }
    UIEdgeInsets insets = view.safeAreaInsets;
    return state.visible && state.size.width == (float)view.bounds.size.width &&
           state.size.height == (float)view.bounds.size.height &&
           state.safeArea.top == (float)insets.top && state.safeArea.bottom == (float)insets.bottom;
}

static void CheckWindow(Program* program, mwinContext* context)
{
    UIView* view = ViewOf(context, program->window);
    mwinWindowState state = {0};
    CHECK(mwinGetWindowState(context, program->window, &state) == mwin_success, "the state");
    CHECK([view.layer isKindOfClass:[CAMetalLayer class]], "the view's layer a CAMetalLayer");
    UIScreen* screen = view.window.windowScene.screen;
    CHECK(state.scale == (float)screen.scale && view.contentScaleFactor == screen.scale,
          "the screen's scale, the view's too");
    CHECK(state.pixelSize.width == (uint32_t)lround(view.bounds.size.width * screen.scale) &&
              state.pixelSize.height == (uint32_t)lround(view.bounds.size.height * screen.scale),
          "the size in pixels");
    CHECK(state.mode == mwin_modeBorderlessFullscreen, "an iPhone's window covers its screen");
    mwinMonitorId monitors[4];
    size_t count = 0;
    mwinMonitorInfo info = {0};
    CHECK(mwinGetMonitors(context, monitors, 4, &count) == mwin_success && count >= 1 &&
              mwinGetMonitorInfo(context, state.monitor, &info) == mwin_success,
          "the window's monitor");
    CHECK(info.primary && info.scale == (float)screen.scale &&
              info.bounds.width == (uint32_t)lround(screen.bounds.size.width * screen.scale),
          "the screen, primary, at its scale");
    if (@available(iOS 16.0, *))
    {
        CHECK(info.hdr.known && info.hdr.headroom >= 1.0f && info.hdr.peakNits == 0.0f &&
                  info.hdr.sdrWhiteNits == 0.0f,
              "extended dynamic range as headroom, no nits");
    }
    CHECK([view.window.windowScene.title isEqual:@"Maul iOS"], "the title the scene's");
    program->scene = view.window.windowScene;
}

// The application goes to the background and comes back as UIKit tells
// the scene's delegate, from outside the frames.
static void Background(UIWindowScene* scene)
{
    id<UIWindowSceneDelegate> delegate = (id<UIWindowSceneDelegate>)scene.delegate;
    dispatch_async(dispatch_get_main_queue(), ^{
      [delegate sceneDidEnterBackground:scene];
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300000000), dispatch_get_main_queue(), ^{
        [delegate sceneWillEnterForeground:scene];
      });
    });
}

// Suspending in a frame of its own; no frames while suspended; the
// suspended record, still waiting when the application resumes, gives
// way to the resuming one (lifecycle records coalesce).
static void CheckLifecycle(const Program* program)
{
    static const mwinEventType order[] = {mwin_eventSuspending, mwin_eventResuming,
                                          mwin_eventResumed};
    bool ordered = program->lifecycle == 3;
    for (int i = 0; ordered && i < 3; i++)
    {
        ordered = program->lifecycleTypes[i] == order[i];
    }
    CHECK(ordered, "suspending, resuming, resumed, and nothing at launch");
    CHECK(ordered && program->lifecycleFrames[1] == program->lifecycleFrames[0] + 1,
          "suspending told in a frame of its own");
    CHECK(ordered && program->lifecycleNs[1] - program->lifecycleNs[0] >= 250000000ull,
          "no frames while suspended");
}

static bool Ready(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
        return program->shown && LaidOut(context, program->window);
    case 1:
        return program->completions >= 3 && program->hidden;
    case 2:
        return program->shown && LaidOut(context, program->window);
    case 3:
        return program->lifecycle >= 3;
    case 4:
        return program->completions >= 1;
    default:
        return program->shown && LaidOut(context, program->window);
    }
}

static void Advance(Program* program, mwinContext* context)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case 0:
        CheckWindow(program, context);
        CHECK(mwinRequestTitle(context, window, "Renamed", 7, nullptr) == mwin_success &&
                  mwinRequestSize(context, window, (mwinSize){100.0f, 100.0f}, nullptr) ==
                      mwin_success &&
                  mwinRequestVisible(context, window, false, nullptr) == mwin_success,
              "a title, a size, hidden");
        break;
    case 1:
        CHECK(program->outcomes[0] == mwin_outcomeDone &&
                  program->outcomes[1] == mwin_outcomeUnsupported &&
                  program->outcomes[2] == mwin_outcomeDone,
              "the title done, the size unsupported, hidden done");
        CHECK([program->scene.title isEqual:@"Renamed"], "the scene's title");
        CHECK(ViewOf(context, window).window.hidden, "the window hidden");
        program->shown = false;
        CHECK(mwinRequestVisible(context, window, true, nullptr) == mwin_success, "shown");
        break;
    case 2:
        Background(program->scene);
        break;
    case 3:
    {
        CheckLifecycle(program);
        CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroyed");
        mwinWindowDef def = mwinDefaultWindowDef();
        CHECK(mwinCreateWindow(context, &def, &program->window, nullptr) == mwin_success,
              "another");
        program->shown = false;
        break;
    }
    case 4:
        CHECK(program->outcomes[0] == mwin_outcomeDone, "made");
        break;
    default:
        CHECK(ViewOf(context, program->window).window.windowScene == program->scene,
              "on the scene the first left");
        break;
    }
    program->phase += 1;
    program->completions = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    printf("init\n");
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul iOS";
    def.titleLength = 8;
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    program->frames += 1;
    Collect(program, context);
    if (Ready(program, context))
    {
        @autoreleasepool
        {
            printf("phase %d\n", program->phase);
            Advance(program, context);
        }
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d, %d lifecycle records\n", program->phase, program->lifecycle);
        s_failures += 1;
        return mwin_frameStop;
    }
    return program->phase == 6 ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    CHECK(status == mwin_success, "init succeeded");
    Program* program = user;
    CHECK(program->phase == 6, "every phase ran");
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
    printf("main\n");
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
