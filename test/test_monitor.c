// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The monitor contract against the test backend: hotplug records, facts
// including HDR, a snapshot with the primary first, changes merged per
// monitor, a window's monitor, ids that stay stale after a disconnect
// until the record is drained, and the monitor limit.

#include "test_program.h"

#include <string.h>

static mwinMonitorInfo Info(const char* name, bool primary, float scale)
{
    mwinMonitorInfo info = {0};
    info.nameLength = (uint32_t)strlen(name);
    memcpy(info.name, name, info.nameLength);
    info.bounds = (mwinPixelRect){0, 0, 2560, 1440};
    info.workArea = (mwinPixelRect){0, 0, 2560, 1400};
    info.scale = scale;
    info.refreshMilliHz = 143856;
    info.primary = primary;
    info.hdr = (mwinHdrFacts){true, true, 1000.0f, 400.0f, 240.0f};
    return info;
}

static bool SameMonitor(mwinMonitorId a, mwinMonitorId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// The monitors a test connects.
static mwinMonitorId s_monitors[4];

static void HotplugStep(Program* program, mwinContext* context, int step)
{
    mwinMonitorInfo info;
    if (step == 0)
    {
        mwinMonitorInfo side = Info("Side", false, 1.0f);
        mwinMonitorInfo main = Info("Main \xC3\x9C", true, 1.5f);
        CHECK(mwinTestAddMonitor(context, &side, &s_monitors[0]) == mwin_success &&
                  mwinTestAddMonitor(context, &main, &s_monitors[1]) == mwin_success,
              "two monitors");
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        CHECK(program->events[0].type == mwin_eventMonitorAdded &&
                  program->events[1].type == mwin_eventMonitorAdded &&
                  SameMonitor(program->events[1].data.monitor, s_monitors[1]) &&
                  program->events[0].window.index1 == 0,
              "hotplug records name the monitor and no window");
        mwinMonitorId listed[4];
        size_t count = 0;
        CHECK(mwinGetMonitors(context, listed, 4, &count) == mwin_success && count == 2 &&
                  SameMonitor(listed[0], s_monitors[1]) && SameMonitor(listed[1], s_monitors[0]),
              "a snapshot, the primary first");
        CHECK(mwinGetMonitors(context, listed, 1, &count) == mwin_errorCapacity && count == 2,
              "a short list says how many");
        CHECK(mwinGetMonitorInfo(context, s_monitors[1], &info) == mwin_success &&
                  info.nameLength == 7 && memcmp(info.name, "Main \xC3\x9C", 7) == 0 &&
                  info.hdr.active && info.hdr.sdrWhiteNits == 240.0f &&
                  info.refreshMilliHz == 143856,
              "its facts");
        mwinWindowState state;
        CHECK(mwinGetWindowState(context, program->windows[0], &state) == mwin_success &&
                  SameMonitor(state.monitor, s_monitors[1]),
              "a new window shows on the primary monitor");
        mwinMonitorInfo changed = Info("Main \xC3\x9C", true, 2.0f);
        CHECK(mwinTestChangeMonitor(context, s_monitors[1], &changed) == mwin_success &&
                  mwinTestChangeMonitor(context, s_monitors[1], &changed) == mwin_success &&
                  mwinTestChangeMonitor(context, s_monitors[0], &changed) == mwin_success,
              "changes");
        mwinEvent move = {.type = mwin_eventDisplayChanged, .window = program->windows[0]};
        move.data.monitor = s_monitors[0];
        CHECK(mwinTestPost(context, &move) == mwin_success, "the window moves");
        return;
    }
    if (step == 2)
    {
        CHECK(program->eventCount == 3 && program->events[0].type == mwin_eventMonitorChanged &&
                  program->events[1].type == mwin_eventMonitorChanged &&
                  !SameMonitor(program->events[0].data.monitor, program->events[1].data.monitor) &&
                  program->events[2].type == mwin_eventDisplayChanged,
              "changes merge per monitor");
        CHECK(mwinTestRemoveMonitor(context, s_monitors[0]) == mwin_success, "unplugged");
        CHECK(mwinGetMonitorInfo(context, s_monitors[0], &info) == mwin_errorStale &&
                  mwinTestRemoveMonitor(context, s_monitors[0]) == mwin_errorStale,
              "its id is stale at once");
        mwinMonitorInfo again = Info("Side", false, 1.0f);
        CHECK(mwinTestAddMonitor(context, &again, &s_monitors[2]) == mwin_success &&
                  s_monitors[2].index1 != s_monitors[0].index1,
              "plugged in again, it gets another slot while its removal waits");
        return;
    }
    CHECK(program->eventCount == 2 && program->events[0].type == mwin_eventMonitorRemoved &&
              SameMonitor(program->events[0].data.monitor, s_monitors[0]) &&
              program->events[1].type == mwin_eventMonitorAdded,
          "the removal, then the new connection");
    program->done = true;
}

static void TestHotplug(void)
{
    Program program = {.step = HotplugStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void LimitStep(Program* program, mwinContext* context, int step)
{
    (void)step;
    mwinMonitorInfo info = Info("M", false, 1.0f);
    mwinMonitorId id;
    CHECK(mwinTestAddMonitor(context, &info, &s_monitors[0]) == mwin_success &&
              mwinTestAddMonitor(context, &info, &s_monitors[1]) == mwin_success,
          "two monitors");
    CHECK(mwinTestAddMonitor(context, &info, &id) == mwin_errorCapacity, "not three");
    mwinMonitorInfo bad = info;
    bad.name[0] = (char)0xFF;
    bad.nameLength = 1;
    CHECK(mwinTestChangeMonitor(context, s_monitors[0], &bad) == mwin_success &&
              mwinGetMonitorInfo(context, s_monitors[0], &info) == mwin_success &&
              info.nameLength == 0,
          "a name that is not UTF-8 is left empty");
    // 62 bytes of "a", then the first two of a three-byte character, as a
    // backend cutting a long name at 64 bytes leaves it.
    mwinMonitorInfo cut = info;
    memset(cut.name, 'a', 62);
    memcpy(cut.name + 62, "\xE3\x81", 2);
    cut.nameLength = MWIN_MONITOR_NAME_BYTES;
    CHECK(mwinTestChangeMonitor(context, s_monitors[1], &cut) == mwin_success &&
              mwinGetMonitorInfo(context, s_monitors[1], &info) == mwin_success &&
              info.nameLength == 62 && info.name[61] == 'a',
          "a name cut inside a character keeps its whole characters");
    cut.name[10] = (char)0xC0;
    CHECK(mwinTestChangeMonitor(context, s_monitors[1], &cut) == mwin_success &&
              mwinGetMonitorInfo(context, s_monitors[1], &info) == mwin_success &&
              info.nameLength == 10,
          "and one ill-formed within, its part before");
    mwinEvent move = {.type = mwin_eventDisplayChanged, .window = Create(context, nullptr)};
    move.data.monitor = (mwinMonitorId){5, 1};
    CHECK(mwinTestPost(context, &move) == mwin_errorStale, "no move to an unknown monitor");
    mwinEvent added = {.type = mwin_eventMonitorAdded};
    CHECK(mwinTestPost(context, &added) == mwin_errorInvalid, "hotplug is not a report");
    program->done = true;
}

static void TestLimit(void)
{
    Program program = {.step = LimitStep};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.monitors = 2;
    CHECK(RunWith(&program, def) == mwin_success, "the program runs");
    def.limits.monitors = 200;
    CHECK(RunWith(&program, def) == mwin_errorInvalid, "notifications must cover the monitors");
}

int main(void)
{
    TestHotplug();
    TestLimit();
    return s_failures == 0 ? 0 : 1;
}
