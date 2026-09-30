// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's facts on macOS against AppKit's own (a CI runner's
// session): the theme the application's appearance matches, the accent
// color in sRGB, reduced motion, a text scale of 1, the power source,
// Low Power Mode, and the preferred languages in order. Their changes
// cannot be made on the runner without changing the user's settings,
// so what watches them only runs.

#include "test_harness.h"

#include "maul-window/system.h"

#import <AppKit/AppKit.h>
#include <math.h>
#include <string.h>

typedef struct Program
{
    int frames;
} Program;

static uint32_t Channel(CGFloat value)
{
    return (uint32_t)lround(value * 255.0);
}

static void CheckFacts(mwinContext* context)
{
    mwinSystemFacts facts;
    CHECK(mwinGetSystemFacts(context, &facts) == mwin_success, "the facts");
    NSAppearanceName name = [NSApp.effectiveAppearance
        bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
    mwinTheme theme = [name isEqual:NSAppearanceNameDarkAqua] ? mwin_themeDark : mwin_themeLight;
    CHECK(facts.theme == theme, "the appearance's theme");
    NSColor* accent = [NSColor.controlAccentColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    uint32_t expected = Channel(accent.redComponent) << 24 | Channel(accent.greenComponent) << 16 |
                        Channel(accent.blueComponent) << 8 | Channel(accent.alphaComponent);
    CHECK(facts.hasAccent && facts.accent == expected, "the accent color, 0xRRGGBBAA in sRGB");
    CHECK(facts.reducedMotion == NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion,
          "reduced motion");
    CHECK(facts.textScale == 1.0f && !facts.snapLayouts, "no text scale, no snap layouts");
    CHECK(facts.onBattery != mwin_yes, "not on battery, a runner being plugged in or without one");
    CHECK(facts.lowPower == (NSProcessInfo.processInfo.lowPowerModeEnabled ? mwin_yes : mwin_no),
          "Low Power Mode");
    printf("theme %d accent %08x on battery %d\n", facts.theme, facts.accent, facts.onBattery);
}

static void CheckLocales(mwinContext* context)
{
    char list[256];
    size_t length = 0;
    NSString* joined = [NSLocale.preferredLanguages componentsJoinedByString:@","];
    CHECK(mwinGetPreferredLocales(context, list, sizeof(list), &length) == mwin_success &&
              length > 0 && length == strlen(joined.UTF8String) &&
              memcmp(list, joined.UTF8String, length) == 0,
          "the preferred languages in order");
    printf("locales %.*s\n", (int)length, list);
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    program->frames += 1;
    if (program->frames < 3)
    {
        return mwin_frameContinue;
    }
    @autoreleasepool
    {
        CheckFacts(context);
        CheckLocales(context);
    }
    return mwin_frameStop;
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
    CHECK(program.frames == 3, "its frames ran");
    return s_failures == 0 ? 0 : 1;
}
