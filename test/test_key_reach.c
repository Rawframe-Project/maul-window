// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The chords each platform keeps (key_reach.c), every table on every
// host: how rules match (needed and allowed modifiers, runs of keys,
// any key but the modifier keys, the first rule winning) and what each
// platform answers for the chords research names.

#include "key_reach.h"
#include "test_harness.h"

enum
{
    shift = mwin_modShift,
    control = mwin_modControl,
    alt = mwin_modAlt,
    meta = mwin_modMeta,
};

static bool Is(const mwinKeyRules* rules, mwinKeyCode code, mwinModifiers modifiers,
               mwinKeyReach reach)
{
    return mwinFindKeyReach(rules, code, modifiers) == reach;
}

static void TestMatching(void)
{
    static const mwinKeyRule rules[] = {
        {mwin_codeKeyA, mwin_codeKeyA, control, shift, mwin_keyReachNever},
        {mwin_codeF1, mwin_codeF3, alt, 0, mwin_keyReachShared},
        {0, 0, meta, shift | control | alt, mwin_keyReachUncertain},
        {mwin_codeKeyA, mwin_codeKeyA, 0, shift | control | alt | meta, mwin_keyReachShared},
    };
    const mwinKeyRules table = {rules, 4};
    CHECK(Is(&table, mwin_codeKeyA, control, mwin_keyReachNever) &&
              Is(&table, mwin_codeKeyA, control | shift, mwin_keyReachNever),
          "the modifiers needed, with those allowed");
    CHECK(Is(&table, mwin_codeKeyA, control | alt, mwin_keyReachShared) &&
              Is(&table, mwin_codeKeyA, 0, mwin_keyReachShared),
          "one not allowed, or one missing, falls to a later rule");
    CHECK(
        Is(&table, mwin_codeKeyA, control | mwin_modCapsLock | mwin_modNumLock, mwin_keyReachNever),
        "the locks ignored");
    CHECK(Is(&table, mwin_codeF1, alt, mwin_keyReachShared) &&
              Is(&table, mwin_codeF3, alt, mwin_keyReachShared) &&
              Is(&table, mwin_codeF4, alt, mwin_keyReachDelivered),
          "a run of keys, ends included");
    CHECK(Is(&table, mwin_codeKeyA, control | meta, mwin_keyReachUncertain) &&
              Is(&table, mwin_codeSpace, meta, mwin_keyReachUncertain),
          "any key, the first rule that matches winning");
    CHECK(Is(&table, mwin_codeMetaLeft, meta, mwin_keyReachDelivered) &&
              Is(&table, mwin_codeControlRight, meta, mwin_keyReachDelivered),
          "any key leaves out the modifier keys");
    CHECK(Is(&table, mwin_codeKeyB, 0, mwin_keyReachDelivered), "no rule: delivered");
}

static void TestWindows(void)
{
    const mwinKeyRules* rules = &mwinKeyRulesWindows;
    CHECK(Is(rules, mwin_codeDelete, control | alt, mwin_keyReachNever) &&
              Is(rules, mwin_codeDelete, control | alt | shift, mwin_keyReachNever) &&
              Is(rules, mwin_codeTab, alt, mwin_keyReachNever) &&
              Is(rules, mwin_codeTab, alt | shift, mwin_keyReachNever) &&
              Is(rules, mwin_codeEscape, control | shift, mwin_keyReachNever) &&
              Is(rules, mwin_codeKeyL, meta, mwin_keyReachNever),
          "Windows: the secure attention sequence and the shell's chords never");
    CHECK(Is(rules, mwin_codeKeyD, meta, mwin_keyReachUncertain) &&
              Is(rules, mwin_codePrintScreen, 0, mwin_keyReachUncertain),
          "Windows: Explorer's hotkeys and the snipping tool uncertain");
    CHECK(Is(rules, mwin_codeF4, alt, mwin_keyReachShared) &&
              Is(rules, mwin_codeSpace, alt, mwin_keyReachShared) &&
              Is(rules, mwin_codeMetaLeft, meta, mwin_keyReachShared),
          "Windows: Alt+F4, Alt+Space and the Windows key shared");
    CHECK(Is(rules, mwin_codeTab, 0, mwin_keyReachDelivered) &&
              Is(rules, mwin_codeKeyW, control, mwin_keyReachDelivered) &&
              Is(rules, mwin_codeF4, control | alt, mwin_keyReachDelivered),
          "Windows: the rest delivered");
}

static void TestApple(void)
{
    CHECK(Is(&mwinKeyRulesMacos, mwin_codeTab, meta, mwin_keyReachNever) &&
              Is(&mwinKeyRulesMacos, mwin_codeTab, meta | shift, mwin_keyReachNever) &&
              Is(&mwinKeyRulesMacos, mwin_codeEscape, meta | alt, mwin_keyReachNever),
          "macOS: the switcher and Force Quit never");
    CHECK(Is(&mwinKeyRulesMacos, mwin_codeSpace, meta, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesMacos, mwin_codeDigit4, meta | shift, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesMacos, mwin_codeArrowLeft, control, mwin_keyReachUncertain),
          "macOS: Spotlight, screenshots and Mission Control uncertain");
    CHECK(Is(&mwinKeyRulesMacos, mwin_codeKeyQ, meta, mwin_keyReachDelivered) &&
              Is(&mwinKeyRulesMacos, mwin_codeDigit6, meta | shift, mwin_keyReachDelivered),
          "macOS: Command+Q delivered with no menu");
    CHECK(Is(&mwinKeyRulesIos, mwin_codeTab, meta, mwin_keyReachNever) &&
              Is(&mwinKeyRulesIos, mwin_codeKeyH, meta, mwin_keyReachNever) &&
              Is(&mwinKeyRulesIos, mwin_codeSpace, meta, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesIos, mwin_codeKeyC, meta, mwin_keyReachDelivered),
          "iOS: the switcher and Home never, Spotlight uncertain");
}

static void TestAndroidAndLinux(void)
{
    CHECK(Is(&mwinKeyRulesAndroid, mwin_codeEnter, meta, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesAndroid, mwin_codeTab, alt, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesAndroid, mwin_codeEscape, 0, mwin_keyReachDelivered),
          "Android: the system's shortcuts uncertain, Escape delivered");
    CHECK(Is(&mwinKeyRulesLinux, mwin_codeKeyA, meta, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesLinux, mwin_codeTab, alt, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesLinux, mwin_codeF2, alt, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesLinux, mwin_codeF12, control | alt, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesLinux, mwin_codePrintScreen, 0, mwin_keyReachUncertain),
          "X11 and Wayland: the desktop's shortcuts uncertain");
    CHECK(Is(&mwinKeyRulesLinux, mwin_codeF4, 0, mwin_keyReachDelivered) &&
              Is(&mwinKeyRulesLinux, mwin_codeKeyW, control, mwin_keyReachDelivered),
          "X11 and Wayland: nothing never");
}

static void TestWeb(void)
{
    CHECK(Is(&mwinKeyRulesChromium, mwin_codeKeyW, control, mwin_keyReachNever) &&
              Is(&mwinKeyRulesChromium, mwin_codeKeyN, control | shift, mwin_keyReachNever) &&
              Is(&mwinKeyRulesChromium, mwin_codeTab, control, mwin_keyReachNever) &&
              Is(&mwinKeyRulesChromium, mwin_codePageDown, control, mwin_keyReachNever) &&
              Is(&mwinKeyRulesChromium, mwin_codeKeyT, meta, mwin_keyReachNever),
          "Chromium: the chords its own UI owns never");
    CHECK(Is(&mwinKeyRulesBrowser, mwin_codeKeyW, control, mwin_keyReachShared) &&
              Is(&mwinKeyRulesChromium, mwin_codeKeyC, control, mwin_keyReachShared) &&
              Is(&mwinKeyRulesBrowser, mwin_codeKeyC, meta | shift, mwin_keyReachShared) &&
              Is(&mwinKeyRulesBrowser, mwin_codeF5, 0, mwin_keyReachShared) &&
              Is(&mwinKeyRulesBrowser, mwin_codeF12, shift, mwin_keyReachShared),
          "the web: the shortcuts left to the browser shared");
    CHECK(Is(&mwinKeyRulesBrowser, mwin_codeEscape, 0, mwin_keyReachUncertain) &&
              Is(&mwinKeyRulesBrowser, mwin_codeKeyC, control | alt, mwin_keyReachDelivered) &&
              Is(&mwinKeyRulesBrowser, mwin_codeControlLeft, control, mwin_keyReachDelivered) &&
              Is(&mwinKeyRulesBrowser, mwin_codeF6, 0, mwin_keyReachDelivered),
          "the web: Escape uncertain, AltGr chords and the rest delivered");
}

static void TestBackends(void)
{
    CHECK(mwinWindowsKeyReach(nullptr, mwin_codeTab, alt) == mwin_keyReachNever &&
              mwinMacosKeyReach(nullptr, mwin_codeTab, meta) == mwin_keyReachNever &&
              mwinIosKeyReach(nullptr, mwin_codeKeyH, meta) == mwin_keyReachNever &&
              mwinAndroidKeyReach(nullptr, mwin_codeTab, alt) == mwin_keyReachUncertain &&
              mwinLinuxKeyReach(nullptr, mwin_codeTab, alt) == mwin_keyReachUncertain,
          "each backend answers from its platform's table");
}

int main(void)
{
    TestMatching();
    TestWindows();
    TestApple();
    TestAndroidAndLinux();
    TestWeb();
    TestBackends();
    return s_failures == 0 ? 0 : 1;
}
