// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's facts, locales and accessibility hooks on iOS, in the
// simulator (tools/run_ios_app.sh): the facts compared with what UIKit
// says (no accent, iOS having none of the user's); the preferred
// languages; the window given the other style, posted as a theme
// change; a client's first question about the view told once, before
// any root; a root as the view's element, held while it is the root,
// let go with no root.

#include "test_harness.h"

#include "maul-window/accessibility.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/system.h"
#include "maul-window/window.h"

#import <UIKit/UIKit.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull

static bool s_freed;

// A root that tells when it is freed.
@interface FakeRoot : UIAccessibilityElement
@end

@implementation FakeRoot
- (void)dealloc
{
    s_freed = true;
    [super dealloc];
}
@end

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    UIView* view;
    bool shown;
    int themes;
    mwinTheme wanted;
    int asked;
    int completions;
    mwinOutcome outcome;
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
        program->themes += event.type == mwin_eventThemeChanged;
        program->asked += event.type == mwin_eventAccessibilityRequested;
        if (event.type == mwin_eventRequestCompleted)
        {
            program->completions += 1;
            program->outcome = event.data.completion.outcome;
        }
    }
}

static void CheckFacts(const Program* program, mwinContext* context)
{
    mwinSystemFacts facts = {0};
    CHECK(mwinGetSystemFacts(context, &facts) == mwin_success, "the facts");
    UITraitCollection* traits = program->view.traitCollection;
    mwinTheme theme =
        traits.userInterfaceStyle == UIUserInterfaceStyleDark ? mwin_themeDark : mwin_themeLight;
    CHECK(facts.theme == theme, "the window's style");
    CHECK(!facts.hasAccent, "no accent of the user's");
    CHECK(facts.reducedMotion == UIAccessibilityIsReduceMotionEnabled(), "reduced motion");
    float scale = (float)([UIFontMetrics.defaultMetrics scaledValueForValue:17.0
                                              compatibleWithTraitCollection:traits] /
                          17.0);
    CHECK(fabsf(facts.textScale - scale) < 0.001f && facts.textScale > 0.0f,
          "Dynamic Type's scale of the body text");
    UIDeviceBatteryState battery = UIDevice.currentDevice.batteryState;
    mwinTristate onBattery = battery == UIDeviceBatteryStateUnplugged ? mwin_yes
                             : battery == UIDeviceBatteryStateUnknown ? mwin_unknown
                                                                      : mwin_no;
    // The simulator cannot watch its battery: unknown there.
    CHECK(facts.onBattery == onBattery, "whether the battery provides the power");
    CHECK(facts.lowPower == (NSProcessInfo.processInfo.lowPowerModeEnabled ? mwin_yes : mwin_no),
          "Low Power Mode");
    char locales[256];
    size_t length = 0;
    NSString* expected = [NSLocale.preferredLanguages componentsJoinedByString:@","];
    CHECK(mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success &&
              length == strlen(expected.UTF8String) &&
              memcmp(locales, expected.UTF8String, length) == 0,
          "the preferred languages");
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->shown && program->view != nil;
    case 1:
        return program->themes >= 1;
    case 2:
        return program->asked >= 1;
    case 3:
    case 5:
        return program->completions >= 1;
    default:
        return true;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    UIView* view = program->view;
    switch (program->phase)
    {
    case 0:
        CheckFacts(program, context);
        program->themes = 0;
        // The style it is not, so that it changes.
        bool dark = view.traitCollection.userInterfaceStyle == UIUserInterfaceStyleDark;
        program->wanted = dark ? mwin_themeLight : mwin_themeDark;
        view.window.overrideUserInterfaceStyle =
            dark ? UIUserInterfaceStyleLight : UIUserInterfaceStyleDark;
        break;
    case 1:
    {
        mwinSystemFacts facts = {0};
        CHECK(mwinGetSystemFacts(context, &facts) == mwin_success && facts.theme == program->wanted,
              "the other style, posted");
        CHECK(view.accessibilityElements.count == 0, "no root, no elements");
        CHECK(!view.isAccessibilityElement, "a container, not a text area");
        break;
    }
    case 2:
    {
        (void)view.accessibilityElements;
        FakeRoot* root = [[FakeRoot alloc] initWithAccessibilityContainer:view];
        root.accessibilityLabel = @"Maul";
        CHECK(mwinRequestAccessibilityRoot(context, program->window, root, nullptr) == mwin_success,
              "a root");
        // The program's own reference goes: the window holds the root.
        [root release];
        program->completions = 0;
        break;
    }
    case 3:
        CHECK(program->asked == 1, "the first question told once, before any root");
        CHECK(program->outcome == mwin_outcomeDone && view.accessibilityElements.count == 1 &&
                  [[view.accessibilityElements[0] accessibilityLabel] isEqual:@"Maul"],
              "the root the view's element");
        CHECK(!s_freed, "held while it is the root");
        break;
    case 4:
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
        program->view = (UIView*)handles.handles.apple.view;
    }
    if (program->phase == 6)
    {
        // The last frame's pool has let go of what the view gave out.
        CHECK(program->outcome == mwin_outcomeDone && s_freed, "no root lets it go");
        return mwin_frameStop;
    }
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
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
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
