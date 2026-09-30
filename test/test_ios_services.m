// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS clipboard and services, in the simulator
// (tools/run_ios_app.sh): text written to the general pasteboard and
// read back, and text another writer put there read; the display kept
// awake while asked, through the idle timer; a mailto address, which the
// simulator has no program for, answered failed when UIKit says so;
// revealing a file unsupported; and message boxes shown from a frame,
// one taken away (closed), one answered OK. Pressing an alert's button
// has no public way: the test calls the button's handler through its
// private key, which only a test may do.

#include "test_harness.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"
#include "maul-window/services.h"
#include "maul-window/window.h"

#import <UIKit/UIKit.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    int completions;
    mwinOutcome outcomes[4];
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
        if (event.type == mwin_eventRequestCompleted && program->completions < 4)
        {
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
    }
}

static bool ClipboardIs(mwinContext* context, const char* expected)
{
    char text[64];
    size_t length = 0;
    return mwinGetClipboardText(context, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

// The alert showing, from the top of the application's windows.
static UIAlertController* AlertShowing(void)
{
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        for (UIWindow* window in ((UIWindowScene*)scene).windows)
        {
            UIViewController* top = window.rootViewController;
            while (top.presentedViewController != nil)
            {
                top = top.presentedViewController;
            }
            if ([top isKindOfClass:[UIAlertController class]])
            {
                return (UIAlertController*)top;
            }
        }
    }
    return nil;
}

// Once the alert shows: presses its first button, or takes it away.
static void Answer(bool press)
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500000000), dispatch_get_main_queue(), ^{
      UIAlertController* alert = AlertShowing();
      if (alert == nil)
      {
          Answer(press);
          return;
      }
      if (press)
      {
          void (^handler)(UIAlertAction*) = [alert.actions[0] valueForKey:@"handler"];
          handler(alert.actions[0]);
      }
      [alert dismissViewControllerAnimated:NO completion:nil];
    });
}

static bool Box(bool press)
{
    mwinMessageBoxDef def = mwinDefaultMessageBoxDef();
    def.title = "Maul";
    def.titleLength = 4;
    def.message = "A question";
    def.messageLength = 10;
    def.buttons = mwin_buttonsOkCancel;
    bool accepted = !press;
    Answer(press);
    CHECK(mwinShowMessageBox(&def, &accepted) == mwin_success, "a message box shown");
    return accepted;
}

static void Advance(Program* program, mwinContext* context)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case 0:
        CHECK(mwinRequestClipboardWrite(context, window, "h\xC3\xA9llo", 6, nullptr) ==
                      mwin_success &&
                  mwinRequestKeepAwake(context, window, true, nullptr) == mwin_success &&
                  mwinRequestRevealFile(context, window, "/tmp", 4, nullptr) == mwin_success,
              "a write, awake, a file shown");
        break;
    case 1:
        CHECK(program->outcomes[0] == mwin_outcomeDone &&
                  program->outcomes[1] == mwin_outcomeDone &&
                  program->outcomes[2] == mwin_outcomeUnsupported,
              "written, awake, no file manager");
        CHECK([UIPasteboard.generalPasteboard.string isEqual:@"héllo"], "on the pasteboard");
        CHECK(UIApplication.sharedApplication.idleTimerDisabled, "the idle timer off");
        UIPasteboard.generalPasteboard.string = @"from the test";
        CHECK(mwinRequestClipboardRead(context, window, nullptr) == mwin_success &&
                  mwinRequestKeepAwake(context, window, false, nullptr) == mwin_success,
              "a read, awake no more");
        break;
    case 2:
        CHECK(program->outcomes[0] == mwin_outcomeDone && ClipboardIs(context, "from the test"),
              "another writer's text read");
        CHECK(!UIApplication.sharedApplication.idleTimerDisabled, "the idle timer on again");
        CHECK(mwinRequestOpenUrl(context, window, "mailto:a@example.com", 20, nullptr) ==
                  mwin_success,
              "an address");
        break;
    case 3:
        CHECK(program->outcomes[0] == mwin_outcomeFailed,
              "no program for it, answered when UIKit says");
        CHECK(!Box(false), "a box taken away reads as closed");
        CHECK(Box(true), "OK accepted");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->completions = 0;
    program->startNs = NowNs();
}

static bool Ready(const Program* program)
{
    static const int answers[] = {0, 3, 2, 1};
    return program->phase == 0 ? program->shown : program->completions >= answers[program->phase];
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
    if (Ready(program))
    {
        printf("phase %d\n", program->phase);
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
    return program->phase == 4 ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase == 4, "every phase ran");
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
