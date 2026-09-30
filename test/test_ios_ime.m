// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on iOS, in the simulator (tools/run_ios_app.sh). The
// test works the view as an input method would, through its text input
// client, one composition a frame, as preedit records coalesce: marked
// text reported, its selection and caret in bytes, the candidate window
// placed at the program's caret; accepted as it is; accepted as other
// text; shortened by a deletion; dropped when the program stops taking
// text; and, while it does not, composed without a record, only the text
// accepted posted.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

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
    int preedits;
    char preedit[32];
    mwinPreeditEvent last;
    mwinPreeditSegment segment;
    int texts;
    char text[32];
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void Copy(char* to, const char* from, uint32_t length)
{
    length = length < 31 ? length : 31;
    memcpy(to, from, length);
    to[length] = '\0';
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        program->completions += event.type == mwin_eventRequestCompleted &&
                                event.data.completion.outcome == mwin_outcomeDone;
        if (event.type == mwin_eventImePreedit)
        {
            program->preedits += 1;
            program->last = event.data.preedit;
            Copy(program->preedit, event.data.preedit.text, event.data.preedit.length);
            program->segment = event.data.preedit.segmentCount > 0 ? event.data.preedit.segments[0]
                                                                   : (mwinPreeditSegment){0};
        }
        if (event.type == mwin_eventTextInput)
        {
            program->texts += 1;
            Copy(program->text, event.data.text.text, event.data.text.length);
        }
    }
}

static UIView<UITextInput>* ViewOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (UIView<UITextInput>*)handles.handles.apple.view
               : nil;
}

static bool Composing(mwinContext* context, mwinWindowId window)
{
    mwinWindowState state = {0};
    return mwinGetWindowState(context, window, &state) == mwin_success && state.composing;
}

static bool Ended(const Program* program)
{
    return program->last.caret == -1 && program->last.length == 0;
}

static void Mark(UIView<UITextInput>* view, NSString* text, NSUInteger at, NSUInteger length)
{
    [view setMarkedText:text selectedRange:NSMakeRange(at, length)];
}

// One step a frame; the checks read what the previous frame's did.
static void Advance(Program* program, mwinContext* context, UIView<UITextInput>* view)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case 0:
        CHECK([view conformsToProtocol:@protocol(UITextInput)], "a text input client");
        CHECK(mwinRequestTextInput(context, window, true, (mwinRect){10.0f, 20.0f, 1.0f, 16.0f},
                                   nullptr) == mwin_success,
              "text taken");
        return;
    case 1:
        Mark(view, @"か", 1, 0);
        CHECK(view.markedTextRange != nil &&
                  [[view textInRange:view.markedTextRange] isEqual:@"か"],
              "the marked text read back");
        CHECK(CGRectEqualToRect([view firstRectForRange:view.markedTextRange],
                                CGRectMake(10.0, 20.0, 1.0, 16.0)),
              "the candidate window at the program's caret");
        break;
    case 2:
        CHECK(strcmp(program->preedit, "\xE3\x81\x8B") == 0 && program->last.caret == 3 &&
                  program->last.segmentCount == 1 && program->segment.start == 0 &&
                  program->segment.length == 3 && program->segment.style == mwin_preeditUnderline,
              "a composition, its caret after it, one clause");
        Mark(view, @"かな", 0, 2);
        break;
    case 3:
        CHECK(strcmp(program->preedit, "\xE3\x81\x8B\xE3\x81\xAA") == 0 &&
                  program->last.selectionStart == 0 && program->last.selectionEnd == 6 &&
                  program->last.caret == 6,
              "its selection in bytes");
        [view unmarkText];
        break;
    case 4:
        CHECK(strcmp(program->text, "\xE3\x81\x8B\xE3\x81\xAA") == 0 && Ended(program) &&
                  view.markedTextRange == nil,
              "accepted as it is");
        Mark(view, @"に", 1, 0);
        [view insertText:@"日"];
        break;
    case 5:
        CHECK(strcmp(program->text, "\xE6\x97\xA5") == 0 && !Composing(context, window) &&
                  view.markedTextRange == nil,
              "accepted as other text");
        Mark(view, @"ab", 2, 0);
        break;
    case 6:
        CHECK(strcmp(program->preedit, "ab") == 0, "composing again");
        [view deleteBackward];
        break;
    case 7:
        CHECK(strcmp(program->preedit, "a") == 0 && program->last.caret == 1,
              "a deletion shortens the composition");
        program->texts = 0;
        CHECK(mwinRequestTextInput(context, window, false, (mwinRect){0}, nullptr) == mwin_success,
              "text no longer taken");
        return;
    case 8:
        CHECK(Ended(program) && !Composing(context, window) && view.markedTextRange == nil &&
                  program->texts == 0,
              "the composition dropped, not accepted");
        program->preedits = 0;
        Mark(view, @"x", 1, 0);
        CHECK(view.markedTextRange != nil, "a method still composes");
        [view insertText:@"x"];
        break;
    default:
        CHECK(program->preedits == 0 && strcmp(program->text, "x") == 0 && program->texts == 1,
              "no record of it; the text accepted posted");
        break;
    }
    program->phase += 1;
}

static bool Ready(const Program* program, UIView* view)
{
    switch (program->phase)
    {
    case 0:
        return program->shown && view != nil && view.isFirstResponder;
    case 1:
    case 8:
        return program->completions >= 1;
    default:
        return true;
    }
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
    UIView<UITextInput>* view = ViewOf(context, program->window);
    if (Ready(program, view))
    {
        printf("phase %d\n", program->phase);
        int phase = program->phase;
        @autoreleasepool
        {
            Advance(program, context, view);
        }
        // A request's step waits for its answer.
        if (phase == 0 || phase == 7)
        {
            program->phase += 1;
            program->completions = 0;
        }
        program->startNs = NowNs();
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d\n", program->phase);
        s_failures += 1;
        return mwin_frameStop;
    }
    return program->phase == 10 ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase == 10, "every phase ran");
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
