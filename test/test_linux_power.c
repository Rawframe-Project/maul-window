// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The power facts, on X11, against the stand-ins of linux_bus_fake.h: a
// portal on a session bus and UPower on a system bus of the test's own.
// - At the start: power saving from the portal, battery from UPower.
// - Each followed as it changes.
// - A change of another interface, or of the wrong type, left alone.
// - With the portal refusing and no system bus: both unknown, and no
//   change told.
// Skipped (exit status 77) without an X server, dbus-daemon or
// libdbus-1.

#include "linux_bus_fake.h"
#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/system.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 10000000000ull
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define MONITOR     "org.freedesktop.portal.PowerProfileMonitor"
#define UPOWER_PATH "/org/freedesktop/UPower"
#define UPOWER      "org.freedesktop.UPower"

static char s_directory[] = "/tmp/mwin-power-XXXXXX";
static char s_system[64];

typedef struct Program
{
    FakeBus* session;
    FakeBus* system;
    int phase;
    int phases;
    int changes;
    int frames;
    uint64_t startNs;
    bool done;
} Program;

// Low power, then on battery, for each phase.
static const mwinTristate s_expected[][2] = {
    {mwin_yes, mwin_no},  {mwin_yes, mwin_yes}, {mwin_no, mwin_yes},
    {mwin_yes, mwin_yes}, {mwin_yes, mwin_no},
};

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

// What the services say once a phase's facts were seen.
static void Change(const Program* program)
{
    unsigned yes = 1;
    unsigned no = 0;
    const char* device = "org.freedesktop.UPower.Device";
    if (program->phase == 0)
    {
        FakePropertyChanged(program->system, UPOWER_PATH, UPOWER, "OnBattery", 'b', &yes);
    }
    else if (program->phase == 1)
    {
        FakePropertyChanged(program->session, PORTAL_PATH, MONITOR, "power-saver-enabled", 'b',
                            &no);
    }
    else if (program->phase == 2)
    {
        // Changes of other interfaces, keys or types, then one to mark
        // them read.
        FakePropertyChanged(program->system, UPOWER_PATH, device, "OnBattery", 'b', &no);
        FakePropertyChanged(program->system, UPOWER_PATH, UPOWER, "OnBattery", 'u', &no);
        FakePropertyChanged(program->system, UPOWER_PATH, UPOWER, "power-saver-enabled", 'b', &no);
        FakePropertyChanged(program->session, PORTAL_PATH, MONITOR, "OnBattery", 'b', &no);
        FakePropertyChanged(program->session, PORTAL_PATH, MONITOR, "power-saver-enabled", 'b',
                            &yes);
    }
    else
    {
        FakePropertyChanged(program->system, UPOWER_PATH, UPOWER, "OnBattery", 'b', &no);
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
    FakePump(program->session);
    FakePump(program->system);
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->changes += event.type == mwin_eventPowerChanged;
    }
    mwinSystemFacts facts;
    CHECK(mwinGetSystemFacts(context, &facts) == mwin_success, "facts");
    program->frames += 1;
    if (program->phases == 0)
    {
        program->done = program->frames > 50;
        CHECK(facts.lowPower == mwin_unknown && facts.onBattery == mwin_unknown &&
                  program->changes == 0,
              "nothing answering leaves both unknown");
    }
    else if (facts.lowPower == s_expected[program->phase][0] &&
             facts.onBattery == s_expected[program->phase][1])
    {
        CHECK(program->changes == 1, "each change told once");
        program->changes = 0;
        program->done = program->phase + 1 == program->phases;
        if (!program->done)
        {
            Change(program);
            program->phase += 1;
        }
    }
    struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, nullptr);
    bool late = NowNs() - program->startNs > DEADLINE_NS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

static void Run(FakeBus* session, FakeBus* system, int phases, const char* what)
{
    Program program = {.session = session, .system = system, .phases = phases};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, what);
}

int main(void)
{
    static FakeBus session;
    static FakeBus system;
    if (getenv("DISPLAY") == nullptr || mkdtemp(s_directory) == nullptr)
    {
        return 77;
    }
    (void)unsetenv("WAYLAND_DISPLAY");
    (void)snprintf(s_system, sizeof(s_system), "%s/system", s_directory);
    (void)mkdir(s_system, 0700);
    system.system = true;
    int status = 77;
    if (FakeStart(&session, s_directory) && FakeStart(&system, s_system))
    {
        session.saver = 1;
        system.battery = 0;
        Run(&session, &system, 5, "the power facts and their changes");
        session.saver = -1;
        (void)setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/mwin", 1);
        Run(&session, nullptr, 0, "no power facts");
        status = s_failures == 0 ? 0 : 1;
    }
    FakeStop(&system);
    FakeStop(&session);
    FakeClean(s_system);
    (void)rmdir(s_system);
    FakeClean(s_directory);
    (void)rmdir(s_directory);
    return status;
}
