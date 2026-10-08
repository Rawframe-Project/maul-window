// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system facts contract against the test backend: the look and the
// power each notify only when they change, the preferred locales, the
// keyboard layout's change, a window's safe area, and the on-screen
// keyboard's request and the part of the window it covers.

#include "test_program.h"

#include <string.h>

static void FactsStep(Program* program, mwinContext* context, int step)
{
    mwinSystemFacts facts;
    if (step == 0)
    {
        CHECK(mwinGetSystemFacts(context, &facts) == mwin_success &&
                  facts.theme == mwin_themeUnknown && facts.textScale == 1.0f &&
                  facts.onBattery == mwin_unknown,
              "nothing known at first");
        CHECK(mwinTestSetLocales(context, "tr-TR,en-GB", 11) == mwin_success, "locales");
        facts.theme = mwin_themeDark;
        facts.hasAccent = true;
        facts.accent = 0x3D7EFFFFu;
        CHECK(mwinTestSetSystemFacts(context, &facts) == mwin_success, "dark");
        facts.textScale = 1.25f;
        CHECK(mwinTestSetSystemFacts(context, &facts) == mwin_success, "and larger text");
        facts.onBattery = mwin_yes;
        CHECK(mwinTestSetSystemFacts(context, &facts) == mwin_success, "on battery");
        CHECK(mwinTestSetSystemFacts(context, &facts) == mwin_success, "the same again");
        // The same again after the facts: a record would move behind them.
        CHECK(mwinTestSetLocales(context, "tr-TR,en-GB", 11) == mwin_success,
              "the same locales again");
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        static const mwinEventType expected[] = {mwin_eventLocaleChanged, mwin_eventThemeChanged,
                                                 mwin_eventPowerChanged};
        CHECK(Types(program, expected, 3), "one record per change, merged, none for no change");
        mwinEvent layout = {.type = mwin_eventKeyboardLayoutChanged};
        CHECK(mwinTestPost(context, &layout) == mwin_success, "the layout changes");
        return;
    }
    CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventKeyboardLayoutChanged,
          "the layout's change comes at the pump");
    CHECK(mwinGetSystemFacts(context, &facts) == mwin_success && facts.textScale == 1.25f &&
              facts.accent == 0x3D7EFFFFu && facts.onBattery == mwin_yes,
          "the facts read back");
    char locales[16];
    size_t length = 0;
    CHECK(mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success &&
              length == 11 && memcmp(locales, "tr-TR,en-GB", 11) == 0,
          "the locales read back");
    CHECK(mwinGetPreferredLocales(context, locales, 3, &length) == mwin_errorCapacity &&
              length == 11,
          "a short buffer");
    CHECK(mwinGetPreferredLocales(context, locales, 11, &length) == mwin_success && length == 11 &&
              mwinGetPreferredLocales(context, nullptr, 0, &length) == mwin_errorCapacity &&
              length == 11,
          "a buffer of the length exactly, and none to measure with");
    CHECK(mwinTestSetLocales(context, "en-GB,tr-TR", 11) == mwin_success &&
              mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success &&
              length == 11 && memcmp(locales, "en-GB,tr-TR", 11) == 0,
          "another order of the same length");
    CHECK(mwinTestSetLocales(context, nullptr, 0) == mwin_success &&
              mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success &&
              length == 0,
          "no locales");
    static char longList[300];
    memset(longList, 'a', sizeof(longList));
    CHECK(mwinTestSetLocales(context, longList, sizeof(longList)) == mwin_errorCapacity &&
              mwinTestSetLocales(context, "\xFF", 1) == mwin_errorInvalid,
          "locales past the limit or not UTF-8");
    mwinEvent theme = {.type = mwin_eventThemeChanged};
    CHECK(mwinTestPost(context, &theme) == mwin_errorInvalid, "facts are set, not reported");
    program->done = true;
}

static void TestFacts(void)
{
    Program program = {.step = FactsStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void WindowFactsStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    mwinWindowState state;
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        mwinEvent safe = {.type = mwin_eventSafeAreaChanged, .window = window};
        safe.data.insets = (mwinInsets){44.0f, 0.0f, 34.0f, 0.0f};
        CHECK(mwinTestPost(context, &safe) == mwin_success, "a notch");
        CHECK(mwinRequestVirtualKeyboard(context, window, true, mwin_purposeEmail,
                                         &program->requests[0]) == mwin_success,
              "show the keyboard for an email address");
        CHECK(mwinRequestVirtualKeyboard(context, window, true, 9, nullptr) == mwin_errorInvalid,
              "an unknown purpose");
        return;
    }
    if (step == 2)
    {
        static const mwinEventType expected[] = {mwin_eventSafeAreaChanged,
                                                 mwin_eventVirtualKeyboardChanged,
                                                 mwin_eventRequestCompleted};
        CHECK(Types(program, expected, 3) &&
                  program->events[2].data.completion.kind == mwin_requestVirtualKeyboard,
              "the safe area, then the keyboard and its completion");
        CHECK(mwinGetWindowState(context, window, &state) == mwin_success &&
                  state.safeArea.top == 44.0f && state.virtualKeyboard.height == 192.0f,
              "the state has both");
        bool visible = false;
        mwinInputPurpose purpose = mwin_purposeText;
        CHECK(mwinTestGetVirtualKeyboard(context, window, &visible, &purpose) == mwin_success &&
                  visible && purpose == mwin_purposeEmail,
              "the test platform tells the keyboard shown for an email address");
        CHECK(mwinTestGetVirtualKeyboard(context, window, nullptr, &purpose) == mwin_errorInvalid &&
                  mwinTestGetVirtualKeyboard(context, (mwinWindowId){0}, &visible, &purpose) ==
                      mwin_errorStale,
              "its reader refuses a NULL out and a window not there");
        CHECK(mwinTestSetAnswer(context, mwin_requestVirtualKeyboard, mwin_outcomeUnsupported) ==
                      mwin_success &&
                  mwinRequestVirtualKeyboard(context, window, false, mwin_purposeUrl, nullptr) ==
                      mwin_success,
              "a platform without one");
        return;
    }
    CHECK(program->eventCount == 1 &&
              program->events[0].data.completion.outcome == mwin_outcomeUnsupported,
          "answers unsupported");
    bool visible = false;
    mwinInputPurpose purpose = mwin_purposeText;
    CHECK(mwinTestGetVirtualKeyboard(context, window, &visible, &purpose) == mwin_success &&
              visible && purpose == mwin_purposeEmail,
          "a request not carried out leaves what it read");
    program->done = true;
}

static void TestWindowFacts(void)
{
    Program program = {.step = WindowFactsStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

int main(void)
{
    TestFacts();
    TestWindowFacts();
    return s_failures == 0 ? 0 : 1;
}
