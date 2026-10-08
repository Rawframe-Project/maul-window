// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's battery against a sysfs tree of the test's own: the
// power supply beside the input device found and read; a charge that
// changes read again, every digit; the path ended where it was found,
// whatever the record held before; none where the device has no power
// supply, its file says no number from 0 to 100, or the root is too long
// for a path.

#include "linux_battery.h"
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char s_root[] = "/tmp/mwin-battery-XXXXXX";

// Makes each folder of a path under the root.
static void Folders(const char* path)
{
    char built[256];
    (void)snprintf(built, sizeof(built), "%s/%s", s_root, path);
    for (char* at = built + strlen(s_root) + 1; *at != '\0'; at++)
    {
        if (*at == '/')
        {
            *at = '\0';
            (void)mkdir(built, 0700);
            *at = '/';
        }
    }
    (void)mkdir(built, 0700);
}

static void Write(const char* path, const char* text)
{
    char full[256];
    (void)snprintf(full, sizeof(full), "%s/%s", s_root, path);
    FILE* file = fopen(full, "w");
    if (file != nullptr)
    {
        (void)fputs(text, file);
        (void)fclose(file);
    }
}

int main(void)
{
    if (mkdtemp(s_root) == nullptr)
    {
        return 77;
    }
    Folders("class/input/event7/device/device/power_supply/ps-controller-battery-0");
    Write("class/input/event7/device/device/power_supply/ps-controller-battery-0/capacity", "73\n");
    Folders("class/input/event8/device/device");
    mwinLinuxBattery battery;
    // What the record held before does not end its path.
    memset(&battery, 'x', sizeof(battery));
    CHECK(mwinLinuxFindBattery(s_root, 7, &battery) && mwinLinuxReadBattery(&battery) == 73,
          "the power supply beside the device, read");
    Write("class/input/event7/device/device/power_supply/ps-controller-battery-0/capacity", "100");
    CHECK(mwinLinuxReadBattery(&battery) == 100, "a full charge, without a newline");
    Write("class/input/event7/device/device/power_supply/ps-controller-battery-0/capacity", "0\n");
    CHECK(mwinLinuxReadBattery(&battery) == 0, "an empty one");
    Write("class/input/event7/device/device/power_supply/ps-controller-battery-0/capacity", "99\n");
    CHECK(mwinLinuxReadBattery(&battery) == 99, "every digit, 9 too");
    static const char* const wrong[] = {"101\n", "1000\n", "-5\n", "abc\n", "", "7x\n"};
    bool refused = true;
    for (size_t i = 0; i < sizeof(wrong) / sizeof(wrong[0]); i++)
    {
        Write("class/input/event7/device/device/power_supply/ps-controller-battery-0/capacity",
              wrong[i]);
        refused = refused && mwinLinuxReadBattery(&battery) == -1;
    }
    CHECK(refused, "no charge from a file that says no number from 0 to 100");
    CHECK(!mwinLinuxFindBattery(s_root, 8, &battery) && battery.path[0] == '\0' &&
              mwinLinuxReadBattery(&battery) == -1,
          "none for a device without a power supply");
    CHECK(!mwinLinuxFindBattery(s_root, 9, &battery), "none for a device not there");
    char longRoot[MWIN_LINUX_BATTERY_PATH];
    memset(longRoot, 'r', sizeof(longRoot) - 1);
    longRoot[sizeof(longRoot) - 1] = '\0';
    CHECK(!mwinLinuxFindBattery(longRoot, 7, &battery) && battery.path[0] == '\0',
          "none past a path's room");
    // The tree removed, deepest first.
    static const char* const made[] = {
        "class/input/event7/device/device/power_supply/ps-controller-battery-0/capacity",
        "class/input/event7/device/device/power_supply/ps-controller-battery-0",
        "class/input/event7/device/device/power_supply",
        "class/input/event7/device/device",
        "class/input/event7/device",
        "class/input/event7",
        "class/input/event8/device/device",
        "class/input/event8/device",
        "class/input/event8",
        "class/input",
        "class",
        "",
    };
    for (size_t i = 0; i < sizeof(made) / sizeof(made[0]); i++)
    {
        char full[256];
        (void)snprintf(full, sizeof(full), "%s/%s", s_root, made[i]);
        (void)remove(full);
    }
    return s_failures == 0 ? 0 : 1;
}
