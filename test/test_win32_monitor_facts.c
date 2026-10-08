// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's reading of what Windows tells of a monitor,
// without a monitor: the name from the display device's description, cut
// to whole characters at the name's bytes and never read past the
// description; the refresh rate from DisplayConfig's ratio, rounded, and
// none from a ratio with a zero; the HDR headroom for SDR output, HDR
// output with and without its luminances, and nothing known. And the
// refresh's bookkeeping, with monitors of the test's own beside the real
// one: one seen first added, seen the same again told nothing, one past
// the slots left out, one whose facts differ changed, one not seen
// removed, and its slot taken by the next.

#include "core.h"
#include "test_harness.h"
#include "win32_output.h"

#include "maul-window/event.h"
#include "maul-window/monitor.h"

#include <string.h>
#include <wchar.h>

static bool Named(const WCHAR* description, size_t capacity, const char* expected)
{
    mwinMonitorInfo info = {.nameLength = 99};
    mwinWin32NameMonitor(description, capacity, &info);
    return info.nameLength == strlen(expected) && memcmp(info.name, expected, info.nameLength) == 0;
}

static void TestName(void)
{
    CHECK(Named(L"Generic PnP Monitor", 128, "Generic PnP Monitor"), "a description as it is");
    CHECK(Named(L"", 128, ""), "none from an empty description");
    static WCHAR longest[128];
    static char cut[MWIN_MONITOR_NAME_BYTES + 1];
    wmemset(longest, L'a', 100);
    memset(cut, 'a', MWIN_MONITOR_NAME_BYTES);
    CHECK(Named(longest, 128, cut), "a long description cut to the name's bytes");
    // 62 bytes, then a character of four that would pass 64.
    static WCHAR emoji[128];
    wmemset(emoji, L'a', 62);
    emoji[62] = 0xD83D;
    emoji[63] = 0xDE00;
    cut[62] = '\0';
    CHECK(Named(emoji, 128, cut), "cut before a character that does not fit, not inside it");
    // A description that fills its array with no terminator, a high
    // surrogate last: the unit past the array is never read as its pair.
    WCHAR full[3] = {L'a', 0xD83D, 0xDE00};
    CHECK(Named(full, 2, "a\xEF\xBF\xBD"), "nothing read past the description");
}

static uint32_t Refresh(UINT32 numerator, UINT32 denominator)
{
    return mwinWin32RefreshOf((DISPLAYCONFIG_RATIONAL){numerator, denominator});
}

static void TestRefresh(void)
{
    CHECK(Refresh(60, 1) == 60000 && Refresh(144000, 1000) == 144000, "whole rates");
    CHECK(Refresh(60000, 1001) == 59940 && Refresh(2, 3) == 667 && Refresh(1, 3) == 333,
          "fractional rates, rounded to the nearest millihertz");
    CHECK(Refresh(0, 1) == 0 && Refresh(60, 0) == 0, "none from a zero");
}

static float Headroom(bool known, bool active, float peak, float white)
{
    mwinHdrFacts hdr = {.known = known, .active = active, .peakNits = peak, .sdrWhiteNits = white};
    mwinWin32SettleHeadroom(&hdr);
    return hdr.headroom;
}

static void TestHeadroom(void)
{
    CHECK(Headroom(true, false, 1000.0f, 0.0f) == 1.0f, "SDR output: nothing brighter than white");
    CHECK(Headroom(false, false, 0.0f, 0.0f) == 0.0f, "nothing known: no headroom");
    CHECK(Headroom(true, true, 1000.0f, 200.0f) == 5.0f, "HDR output: the peak over white");
    CHECK(Headroom(true, true, 100.0f, 200.0f) == 1.0f, "a peak under white: 1");
    CHECK(Headroom(true, true, 0.0f, 200.0f) == 0.0f && Headroom(true, true, 1000.0f, 0.0f) == 0.0f,
          "HDR output without its luminances: unknown");
}

// Whether the monitor records drained since the last call are these
// many added, changed and removed.
static bool Drained(mwinContext* context, int added, int changed, int removed)
{
    int counts[3] = {0, 0, 0};
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        counts[0] += event.type == mwin_eventMonitorAdded;
        counts[1] += event.type == mwin_eventMonitorChanged;
        counts[2] += event.type == mwin_eventMonitorRemoved;
    }
    return counts[0] == added && counts[1] == changed && counts[2] == removed;
}

static size_t Monitors(mwinContext* context)
{
    mwinMonitorId monitors[4];
    size_t count = 0;
    return mwinGetMonitors(context, monitors, 4, &count) == mwin_success ? count : 99;
}

// A refresh that sees the real monitor as it is and the test's own.
static void SeeAll(mwinContext* context, HMONITOR first, const mwinMonitorInfo* firstInfo,
                   HMONITOR second, const mwinMonitorInfo* secondInfo)
{
    mwinWin32Platform* platform = context->backendData;
    mwinMonitorId real;
    size_t count = 0;
    mwinMonitorInfo realInfo;
    (void)mwinGetMonitors(context, &real, 1, &count);
    (void)mwinGetMonitorInfo(context, real, &realInfo);
    mwinWin32BeginMonitors(platform);
    mwinWin32SeeMonitor(platform, MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY),
                        &realInfo);
    if (first != nullptr)
    {
        mwinWin32SeeMonitor(platform, first, firstInfo);
    }
    if (second != nullptr)
    {
        mwinWin32SeeMonitor(platform, second, secondInfo);
    }
    mwinWin32EndMonitors(platform);
}

static mwinFrameResult Slots(mwinContext* context, void* user)
{
    *(bool*)user = true;
    (void)Drained(context, 0, 0, 0);
    HMONITOR one = (HMONITOR)(uintptr_t)0x1000;
    HMONITOR two = (HMONITOR)(uintptr_t)0x2000;
    mwinMonitorInfo a = {
        .bounds = {2000, 0, 800, 600}, .workArea = {2000, 0, 800, 560}, .scale = 1.0f};
    mwinMonitorInfo b = a;
    b.bounds.width = 1024;
    CHECK(Monitors(context) == 1, "the real monitor, alone");
    SeeAll(context, one, &a, nullptr, nullptr);
    CHECK(Drained(context, 1, 0, 0) && Monitors(context) == 2, "one seen first added");
    SeeAll(context, one, &a, two, &b);
    CHECK(Drained(context, 0, 0, 0) && Monitors(context) == 2,
          "seen the same again: nothing; one past the slots left out");
    SeeAll(context, one, &b, nullptr, nullptr);
    CHECK(Drained(context, 0, 1, 0), "one whose facts differ changed");
    SeeAll(context, nullptr, nullptr, nullptr, nullptr);
    CHECK(Drained(context, 0, 0, 1) && Monitors(context) == 1, "one not seen removed");
    SeeAll(context, two, &b, nullptr, nullptr);
    CHECK(Drained(context, 1, 0, 0) && Monitors(context) == 2, "its slot taken by the next");
    SeeAll(context, nullptr, nullptr, nullptr, nullptr);
    CHECK(Drained(context, 0, 0, 1) && Monitors(context) == 1, "and that one removed too");
    return mwin_frameStop;
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_success;
}

int main(void)
{
    TestName();
    TestRefresh();
    TestHeadroom();
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Slots;
    // The real monitor and one of the test's.
    def.context.limits.monitors = 2;
    bool ran = false;
    def.user = &ran;
    CHECK(mwinRun(&def) == mwin_success && ran, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
