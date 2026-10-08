// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's reading of what Windows reports, without Windows'
// settings: the power source from each AC line state and battery flag
// (no battery, the flag unknown, a battery charging), battery saver; the
// text scale at the ends of its range and past them; the preferred UI
// languages joined with commas, one language, and none.

#include "test_harness.h"
#include "win32_system.h"

#include <wchar.h>

static mwinSystemFacts Power(BYTE line, BYTE battery, BYTE saver)
{
    SYSTEM_POWER_STATUS power = {.ACLineStatus = line, .BatteryFlag = battery};
    power.SystemStatusFlag = saver;
    mwinSystemFacts facts = {0};
    mwinWin32PowerFacts(&power, &facts);
    return facts;
}

static void TestPower(void)
{
    CHECK(Power(0, 1, 0).onBattery == mwin_yes && Power(0, 1, 0).lowPower == mwin_no,
          "off the line: on the battery, no saver");
    CHECK(Power(1, 8, 0).onBattery == mwin_no, "on the line, the battery charging: not on it");
    CHECK(Power(255, 1, 0).onBattery == mwin_unknown, "the line unknown: unknown");
    CHECK(Power(0, 128, 0).onBattery == mwin_no, "no battery: never on one");
    CHECK(Power(0, 255, 0).onBattery == mwin_yes, "the battery flag unknown: the line tells");
    CHECK(Power(0, 1, 1).lowPower == mwin_yes, "battery saver on");
}

static void TestTextScale(void)
{
    CHECK(mwinWin32TextScaleOf(100) == 1.0f && mwinWin32TextScaleOf(225) == 2.25f &&
              mwinWin32TextScaleOf(500) == 5.0f,
          "the setting's percent, to 500");
    CHECK(mwinWin32TextScaleOf(99) == 1.0f && mwinWin32TextScaleOf(501) == 1.0f &&
              mwinWin32TextScaleOf(0) == 1.0f,
          "past the range: no setting");
}

static void TestLocales(void)
{
    WCHAR two[] = L"en-US\0tr-TR\0";
    uint32_t length = mwinWin32JoinLocales(two, (ULONG)(sizeof(two) / sizeof(two[0])));
    CHECK(length == 11 && wmemcmp(two, L"en-US,tr-TR", 11) == 0 && two[11] == 0,
          "two languages joined with a comma");
    WCHAR one[] = L"de-DE\0";
    length = mwinWin32JoinLocales(one, (ULONG)(sizeof(one) / sizeof(one[0])));
    CHECK(length == 5 && wmemcmp(one, L"de-DE", 5) == 0 && one[5] == 0, "one language");
    WCHAR none[] = L"\0";
    CHECK(mwinWin32JoinLocales(none, 2) == 0 && mwinWin32JoinLocales(none, 1) == 0 &&
              mwinWin32JoinLocales(none, 0) == 0,
          "none");
}

int main(void)
{
    TestPower();
    TestTextScale();
    TestLocales();
    return s_failures == 0 ? 0 : 1;
}
