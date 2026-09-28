// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The desktop's look and motion from the Settings portal, on X11,
// against the stand-in of linux_bus_fake.h:
// - GNOME's answer: the dark style, the accent, reduced motion from
//   animations off, and large text;
// - the standard reduced-motion key over GNOME's own;
// - the light style, and an accent out of range as none;
// - settings of the wrong type left alone;
// - KDE's answer: no preference as an unknown theme, and reduced motion
//   from an animation factor of 0 given as text ("0.0"), then of 1.5;
//   factors of text that is no number as none; a factor of 0 as a
//   number;
// - no answer: the facts left as they were, and no change told.
// Skipped (exit status 77) without an X server, dbus-daemon or
// libdbus-1.

#include "linux_bus_fake.h"
#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/system.h"

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 10000000000ull
#define APPEARANCE  "org.freedesktop.appearance"

static char s_directory[] = "/tmp/mwin-settings-XXXXXX";

typedef struct Expected
{
    mwinTheme theme;
    bool hasAccent;
    uint32_t accent;
    bool reducedMotion;
    float textScale;
} Expected;

typedef struct Program
{
    FakeBus* fake;
    int phase;
    int phases;
    int changes;
    int frames;
    uint64_t startNs;
    bool done;
} Program;

static const Expected s_gnome[] = {
    {mwin_themeDark, true, 0x8040FFFFu, true, 1.25f},
    {mwin_themeDark, true, 0x8040FFFFu, false, 1.25f},
    {mwin_themeLight, false, 0, false, 1.25f},
    {mwin_themeLight, false, 0, false, 1.0f},
};

static const Expected s_kde[] = {
    {mwin_themeUnknown, false, 0, true, 1.0f}, {mwin_themeUnknown, false, 0, false, 1.0f},
    {mwin_themeDark, false, 0, false, 1.0f},   {mwin_themeLight, false, 0, false, 1.0f},
    {mwin_themeLight, false, 0, true, 1.0f},
};

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static bool Is(const mwinSystemFacts* facts, const Expected* expected)
{
    return facts->theme == expected->theme && facts->hasAccent == expected->hasAccent &&
           facts->accent == expected->accent && facts->reducedMotion == expected->reducedMotion &&
           facts->textScale == expected->textScale;
}

// What the portal says once a phase's facts were seen.
static void Change(FakeBus* fake, int phase)
{
    uint32_t none = 0;
    uint32_t dark = 1;
    uint32_t light = 2;
    const double far[3] = {2.0, 0.0, 0.0};
    const char* darkText = "1";
    const double plain = 1.0;
    // A factor, and the theme that marks the phase.
    const char* factors[3] = {"1.5", ".", "0x"};
    const double off = 0.0;
    if (fake->desktop == fakeKde && phase < 3)
    {
        FakeSettingChanged(fake, "org.kde.kdeglobals.KDE", "AnimationDurationFactor", 's',
                           (const void*)&factors[phase]);
        if (phase > 0)
        {
            FakeSettingChanged(fake, APPEARANCE, "color-scheme", 'u', phase == 1 ? &dark : &light);
        }
    }
    else if (fake->desktop == fakeKde)
    {
        FakeSettingChanged(fake, "org.kde.kdeglobals.KDE", "AnimationDurationFactor", 'd', &off);
    }
    else if (phase == 0)
    {
        FakeSettingChanged(fake, APPEARANCE, "reduced-motion", 'u', &none);
    }
    else if (phase == 1)
    {
        FakeSettingChanged(fake, APPEARANCE, "color-scheme", 'u', &light);
        FakeSettingChanged(fake, APPEARANCE, "accent-color", 'r', far);
    }
    else
    {
        FakeSettingChanged(fake, APPEARANCE, "color-scheme", 's', (const void*)&darkText);
        FakeSettingChanged(fake, "org.gnome.desktop.interface", "text-scaling-factor", 'd', &plain);
        FakeSettingChanged(fake, "org.gnome.desktop.interface", "text-scaling-factor", 's',
                           (const void*)&darkText);
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    Program* program = user;
    program->startNs = NowNs();
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    FakePump(program->fake);
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->changes += event.type == mwin_eventThemeChanged;
    }
    mwinSystemFacts facts;
    CHECK(mwinGetSystemFacts(context, &facts) == mwin_success, "facts");
    const Expected* expected = program->fake->desktop == fakeGnome ? s_gnome : s_kde;
    program->frames += 1;
    if (program->fake->desktop == fakeNoSettings)
    {
        program->done = program->fake->readAlls == 1 && program->frames > 50;
        CHECK(facts.theme == mwin_themeUnknown && facts.textScale == 1.0f && program->changes == 0,
              "no answer leaves the facts as they were");
    }
    else if (Is(&facts, &expected[program->phase]))
    {
        CHECK(program->changes == 1, "each change told once");
        program->changes = 0;
        program->done = program->phase + 1 == program->phases;
        if (!program->done)
        {
            Change(program->fake, program->phase);
            program->phase += 1;
        }
    }
    struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, nullptr);
    bool late = NowNs() - program->startNs > DEADLINE_NS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

static void Run(FakeBus* fake, int desktop, int phases, const char* what)
{
    fake->desktop = desktop;
    fake->readAlls = 0;
    Program program = {.fake = fake, .phases = phases};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, what);
}

int main(void)
{
    static FakeBus fake;
    if (getenv("DISPLAY") == nullptr || mkdtemp(s_directory) == nullptr)
    {
        return 77;
    }
    (void)unsetenv("WAYLAND_DISPLAY");
    int status = 77;
    if (FakeStart(&fake, s_directory))
    {
        Run(&fake, fakeGnome, 4, "GNOME's settings and their changes");
        Run(&fake, fakeKde, 5, "KDE's settings and their changes");
        Run(&fake, fakeNoSettings, 0, "no settings");
        status = s_failures == 0 ? 0 : 1;
    }
    FakeStop(&fake);
    FakeClean(s_directory);
    (void)rmdir(s_directory);
    return status;
}
