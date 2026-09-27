// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's system facts: read at start as the user's
// registry and settings say, and read again when Windows announces a
// change. The test sets a dark theme, an accent, a text scale and no
// animations, tells its window, checks the facts, the record and the
// title bar, and puts every setting back.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/system.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// After windows.h, whose types it uses.
#include <dwmapi.h>

#define DEADLINE_MS 10000u

// DWMWA_USE_IMMERSIVE_DARK_MODE, which older SDKs and mingw lack.
#define DARK_MODE_ATTRIBUTE 20

#define PERSONALIZE   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"
#define DWM           L"Software\\Microsoft\\Windows\\DWM"
#define ACCESSIBILITY L"Software\\Microsoft\\Accessibility"

// A setting of the user's registry, and whether it was set before.
typedef struct Setting
{
    LPCWSTR key;
    LPCWSTR name;
    DWORD value;
    bool present;
} Setting;

typedef enum Phase
{
    phaseCreate,
    phaseChange,
    phaseRestore,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    ULONGLONG startMs;
    mwinWindowId window;
    HWND hwnd;
    Setting settings[3];
    BOOL animations;
    bool animationsSet;
    bool changed;
    bool timedOut;
} Program;

static void Save(Setting* setting)
{
    DWORD size = sizeof(setting->value);
    setting->present =
        RegGetValueW(HKEY_CURRENT_USER, setting->key, setting->name, RRF_RT_REG_DWORD, nullptr,
                     &setting->value, &size) == ERROR_SUCCESS;
}

static void Set(const Setting* setting, DWORD value)
{
    CHECK(RegSetKeyValueW(HKEY_CURRENT_USER, setting->key, setting->name, REG_DWORD, &value,
                          sizeof(value)) == ERROR_SUCCESS,
          "a setting of the user's");
}

static void Restore(const Setting* setting)
{
    if (setting->present)
    {
        Set(setting, setting->value);
    }
    else
    {
        (void)RegDeleteKeyValueW(HKEY_CURRENT_USER, setting->key, setting->name);
    }
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->changed |= event.type == mwin_eventThemeChanged;
        if (event.type == mwin_eventShown && program->phase == phaseCreate)
        {
            program->changed = true;
        }
    }
}

static void CheckStart(mwinContext* context, const Program* program)
{
    mwinSystemFacts facts;
    CHECK(mwinGetSystemFacts(context, &facts) == mwin_success, "the facts");
    const Setting* theme = &program->settings[0];
    CHECK(facts.theme == (!theme->present     ? mwin_themeUnknown
                          : theme->value != 0 ? mwin_themeLight
                                              : mwin_themeDark),
          "the theme as the registry has it");
    CHECK(facts.onBattery <= mwin_yes && facts.lowPower <= mwin_yes, "power facts");
    WCHAR first[LOCALE_NAME_MAX_LENGTH * 4] = {0};
    ULONG count = 0;
    ULONG units = sizeof(first) / sizeof(first[0]);
    char locales[256];
    size_t length = 0;
    CHECK(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, first, &units) &&
              mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success,
          "the preferred locales");
    char expected[LOCALE_NAME_MAX_LENGTH] = {0};
    int bytes =
        WideCharToMultiByte(CP_UTF8, 0, first, -1, expected, sizeof(expected), nullptr, nullptr);
    CHECK(bytes > 1 && length >= (size_t)bytes - 1 &&
              memcmp(locales, expected, (size_t)bytes - 1) == 0,
          "the first locale is Windows' first language");
}

static void CheckChanged(mwinContext* context, const Program* program)
{
    mwinSystemFacts facts;
    CHECK(mwinGetSystemFacts(context, &facts) == mwin_success && facts.theme == mwin_themeDark &&
              facts.hasAccent && facts.accent == 0x112233FFu && facts.textScale == 1.5f,
          "a dark theme, the accent and the text scale");
    CHECK(!program->animationsSet || facts.reducedMotion, "no animations: less motion");
    // Where Windows reports the attribute, the frame is dark.
    BOOL dark = FALSE;
    CHECK(DwmGetWindowAttribute(program->hwnd, DARK_MODE_ATTRIBUTE, &dark, sizeof(dark)) != S_OK ||
              dark,
          "a dark title bar");
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
    {
        mwinNativeHandles handles;
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success,
              "the window's HWND");
        program->hwnd = handles.handles.win32.hwnd;
        CheckStart(context, program);
        Set(&program->settings[0], 0);
        Set(&program->settings[1], 0xFF332211u);
        Set(&program->settings[2], 150);
        program->animationsSet =
            SystemParametersInfoW(SPI_SETCLIENTAREAANIMATION, 0, (PVOID)(UINT_PTR)FALSE, 0);
        SendMessageW(program->hwnd, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
        break;
    }
    case phaseChange:
        CheckChanged(context, program);
        for (int i = 0; i < 3; i++)
        {
            Restore(&program->settings[i]);
        }
        if (program->animationsSet)
        {
            (void)SystemParametersInfoW(SPI_SETCLIENTAREAANIMATION, 0,
                                        (PVOID)(UINT_PTR)program->animations, 0);
        }
        SendMessageW(program->hwnd, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
        break;
    default:
        CheckStart(context, program);
        break;
    }
    program->phase += 1;
    program->changed = false;
    program->startMs = GetTickCount64();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startMs = GetTickCount64();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (program->changed)
    {
        Advance(program, context);
    }
    else if (GetTickCount64() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        Sleep(1);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {
        .settings = {{PERSONALIZE, L"AppsUseLightTheme", 0, false},
                     {DWM, L"AccentColor", 0, false},
                     {ACCESSIBILITY, L"TextScaleFactor", 0, false}},
    };
    for (int i = 0; i < 3; i++)
    {
        Save(&program.settings[i]);
    }
    program.animations = TRUE;
    (void)SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &program.animations, 0);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on Windows");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
