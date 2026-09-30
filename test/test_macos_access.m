// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility hooks on macOS against AppKit (a CI runner's session):
// a client's first question about a window tells the program, once,
// before any root; a root handed over is the view's child and answers
// what is focused and what is under a point, and the window holds it
// while it is the root; no root lets it go. The test asks the view as a
// client would.

#include "test_harness.h"

#include "maul-window/accessibility.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull

static bool s_freed;

// A root that answers with its child, and tells when it is freed.
@interface FakeRoot : NSAccessibilityElement
{
  @public
    NSAccessibilityElement* child;
}
@end

@implementation FakeRoot
- (id)accessibilityFocusedUIElement
{
    return child;
}

- (id)accessibilityHitTest:(NSPoint)point
{
    (void)point;
    return child;
}

- (void)dealloc
{
    s_freed = true;
    [child release];
    [super dealloc];
}
@end

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    NSView* view;
    FakeRoot* root;
    bool shown;
    int asked;
    int completions;
    mwinOutcome outcome;
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
        program->asked += event.type == mwin_eventAccessibilityRequested;
        if (event.type == mwin_eventRequestCompleted)
        {
            program->completions += 1;
            program->outcome = event.data.completion.outcome;
        }
    }
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->shown;
    case 1:
        return program->asked >= 1;
    case 2:
    case 4:
        return program->completions >= 1;
    default:
        return true;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    NSView* view = program->view;
    switch (program->phase)
    {
    case 0:
        CHECK(view != nil && view.accessibilityChildren.count == 0, "no root, no children");
        break;
    case 1:
    {
        (void)view.accessibilityChildren;
        FakeRoot* root = [[FakeRoot alloc] init];
        root.accessibilityRole = NSAccessibilityGroupRole;
        root.accessibilityParent = view;
        root->child = [[NSAccessibilityElement alloc] init];
        program->root = root;
        CHECK(mwinRequestAccessibilityRoot(context, program->window, root, nullptr) == mwin_success,
              "a root");
        break;
    }
    case 2:
    {
        FakeRoot* root = program->root;
        CHECK(program->asked == 1, "the first question told once, before any root");
        CHECK(program->outcome == mwin_outcomeDone &&
                  [view.accessibilityChildren isEqual:@[ root ]],
              "the root the view's child");
        CHECK(view.accessibilityFocusedUIElement == root->child &&
                  [view accessibilityHitTest:NSMakePoint(10.0, 10.0)] == root->child,
              "the root answers what is focused and what is under a point");
        // The program's own reference goes: the window holds the root.
        [root release];
        program->root = nil;
        break;
    }
    case 3:
        CHECK(!s_freed, "held while it is the root");
        program->completions = 0;
        CHECK(mwinRequestAccessibilityRoot(context, program->window, nullptr, nullptr) ==
                  mwin_success,
              "no root");
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
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS accessibility";
    def.titleLength = 24;
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    mwinNativeHandles handles;
    if (program->view == nil &&
        mwinGetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        program->view = (NSView*)handles.handles.apple.view;
    }
    if (program->phase == 5)
    {
        // The last frame's pool has let go of what the view gave out.
        CHECK(program->outcome == mwin_outcomeDone && s_freed &&
                  program->view.accessibilityChildren.count == 0,
              "no root lets it go");
        return mwin_frameStop;
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
    return mwin_frameContinue;
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
    CHECK(program.phase == 5, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
