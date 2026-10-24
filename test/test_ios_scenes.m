// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// iPadOS scenes, in an iPad simulator (tools/run_ios_app.sh): a window
// made on the launch scene; a second one, with no scene waiting, made on
// a new scene UIKit connects for it; that scene closed by the system as
// the user would close it, which asks its window to close, fails a
// request for it and is never taken again; a third window on another new
// scene; that window destroyed while the first remains, which lets its
// scene go. After a scene goes, the test brings the first one forward, as
// the user would, so the application runs again. Each step and each
// scene's change print when they came. mwinRun never returns on iOS: quit prints the closing
// line the runner waits for.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#import <UIKit/UIKit.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 15000000000ull
#define WINDOWS     3

typedef struct Program
{
    int phase;
    uint64_t startNs;
    uint64_t launchNs;
    // The windows in the order made, the outcome of each one's creation
    // (-1 before it), and whether each was asked to close.
    mwinWindowId windows[WINDOWS];
    int created[WINDOWS];
    bool closeRequested[WINDOWS];
    // A request's outcome for the second window once its scene went.
    int failed;
    mwinRequestId failing;
    UIWindowScene* scenes[WINDOWS];
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static bool Same(mwinWindowId a, mwinWindowId b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        for (int i = 0; i < WINDOWS; i++)
        {
            if (!Same(event.window, program->windows[i]))
            {
                continue;
            }
            if (event.type == mwin_eventCloseRequested)
            {
                printf("close requested for window %d\n", i);
                program->closeRequested[i] = true;
            }
            if (event.type == mwin_eventRequestCompleted &&
                event.data.completion.kind == mwin_requestCreate)
            {
                printf("window %d created: outcome %d\n", i, (int)event.data.completion.outcome);
                program->created[i] = (int)event.data.completion.outcome;
            }
        }
        if (event.type == mwin_eventRequestCompleted &&
            memcmp(&event.data.completion.request, &program->failing, sizeof(mwinRequestId)) == 0)
        {
            program->failed = (int)event.data.completion.outcome;
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

static UIWindowScene* SceneOf(mwinContext* context, mwinWindowId window)
{
    return ViewOf(context, window).window.windowScene;
}

static bool Connected(UIScene* scene)
{
    return [UIApplication.sharedApplication.connectedScenes containsObject:scene];
}

// Brings a scene forward, as the user would.
static void Forward(UIWindowScene* scene)
{
    [UIApplication.sharedApplication
        requestSceneSessionActivation:scene.session
                         userActivity:nil
                              options:nil
                         errorHandler:^(NSError* error) {
                           printf("bringing forward failed: %s\n",
                                  error.localizedDescription.UTF8String);
                         }];
}

// Says what each scene does, with its title and when; a scene other than
// the first going away brings the first forward, as the user would: the
// application leaves the foreground when its frontmost scene goes, and
// its frames pause until a scene comes back.
static void Watch(Program* program)
{
    NSNotificationName names[] = {UISceneWillEnterForegroundNotification,
                                  UISceneDidEnterBackgroundNotification,
                                  UISceneDidDisconnectNotification};
    const char* said[] = {"foreground", "background", "disconnected"};
    for (int i = 0; i < 3; i++)
    {
        const char* what = said[i];
        bool gone = i == 2;
        [NSNotificationCenter.defaultCenter
            addObserverForName:names[i]
                        object:nil
                         queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification* note) {
                      UIWindowScene* scene = note.object;
                      printf("scene \"%s\" %s at %.2f s\n", scene.title.UTF8String, what,
                             (double)(NowNs() - program->launchNs) / 1e9);
                      if (gone && scene != program->scenes[0])
                      {
                          Forward(program->scenes[0]);
                      }
                    }];
    }
}

static mwinResult Create(Program* program, mwinContext* context, int index)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    char title[16];
    int length = snprintf(title, sizeof(title), "Scene %d", index);
    def.title = title;
    def.titleLength = (uint32_t)length;
    program->created[index] = -1;
    return mwinCreateWindow(context, &def, &program->windows[index], nullptr);
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->created[0] >= 0;
    case 1:
        return program->created[1] >= 0;
    case 2:
        return program->closeRequested[1];
    case 3:
        return program->failed >= 0 && program->created[2] >= 0;
    default:
        // The third window's scene let go.
        return !Connected(program->scenes[2]);
    }
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
        CHECK(program->created[0] == mwin_outcomeDone, "the first window on the launch scene");
        program->scenes[0] = SceneOf(context, program->windows[0]);
        Watch(program);
        printf("launch scene %p, multiple scenes %d, %lu connected\n", (void*)program->scenes[0],
               (int)UIApplication.sharedApplication.supportsMultipleScenes,
               (unsigned long)UIApplication.sharedApplication.connectedScenes.count);
        CHECK(Create(program, context, 1) == mwin_success, "a second window asked for");
        break;
    case 1:
        CHECK(program->created[1] == mwin_outcomeDone, "the second window made");
        program->scenes[1] = [SceneOf(context, program->windows[1]) retain];
        CHECK(program->scenes[1] != nil && program->scenes[1] != program->scenes[0] &&
                  Connected(program->scenes[1]) && Connected(program->scenes[0]),
              "on a new scene, the first still connected");
        CHECK([program->scenes[1].title isEqual:@"Scene 1"], "the new scene's title the window's");
        // The system closes it, as the user would.
        [UIApplication.sharedApplication
            requestSceneSessionDestruction:program->scenes[1].session
                                   options:nil
                              errorHandler:^(NSError* error) {
                                printf("closing failed: %s\n",
                                       error.localizedDescription.UTF8String);
                              }];
        break;
    case 2:
        program->failed = -1;
        CHECK(mwinRequestTitle(context, program->windows[1], "Gone", 4, &program->failing) ==
                  mwin_success,
              "a title for the window whose scene went");
        CHECK(mwinDestroyWindow(context, program->windows[1]) == mwin_success &&
                  Create(program, context, 2) == mwin_success,
              "the window destroyed, a third asked for");
        break;
    case 3:
        // The title's request was cancelled by the destruction, or failed
        // before it.
        CHECK(program->failed == mwin_outcomeFailed || program->failed == mwin_outcomeCancelled,
              "nothing done for a window whose scene went");
        CHECK(program->created[2] == mwin_outcomeDone, "the third window made");
        program->scenes[2] = [SceneOf(context, program->windows[2]) retain];
        CHECK(program->scenes[2] != nil && program->scenes[2] != program->scenes[1] &&
                  program->scenes[2] != program->scenes[0],
              "on a scene of its own, not the one that went");
        CHECK(mwinDestroyWindow(context, program->windows[2]) == mwin_success,
              "the third destroyed while the first remains");
        break;
    default:
        CHECK(Connected(program->scenes[0]), "the first scene still connected");
        break;
    }
    printf("phase %d at %.2f s\n", program->phase, (double)(NowNs() - program->launchNs) / 1e9);
    program->phase += 1;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    printf("init, %s\n", UIDevice.currentDevice.model.UTF8String);
    program->launchNs = NowNs();
    program->startNs = program->launchNs;
    program->failed = -1;
    return Create(program, context, 0);
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
        printf("timed out in phase %d\n", program->phase);
        s_failures += 1;
        return mwin_frameStop;
    }
    return program->phase == 5 ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    CHECK(status == mwin_success, "init succeeded");
    Program* program = user;
    CHECK(program->phase == 5, "every phase ran");
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
