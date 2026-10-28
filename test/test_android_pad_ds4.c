// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A DualShock 4 in the emulator (tools/run_android_app.sh), made over USB
// by Android's hid tool through the kernel's uhid with the descriptor and
// feature reports of Android's own CTS script for it, so that the
// kernel's driver (hid-sony, or hid-playstation from Linux 5.12) makes
// its motors, motion sensors and battery as for a real one. The pad is
// added with its ids and rumble, once Android's input service lists it.
// Asked to rumble, heavy at a quarter and light in full, for longer than
// the test runs, then to stop, the pad gets what was asked, as Android
// reads it: from Android 12, where the pad has two motors, the driver's
// output reports (the hid tool prints them) carry the light motor at 255
// and the heavy one at 64, then both at 0; on Android 11, whose one
// vibrator runs at the stronger and whose hid tool prints no output, the
// input service says the pad vibrates, then that it does not. From
// Android 12 the pad has motion, at once or by a change a moment after
// it is added, as Android merges the driver's motion device into it:
// turned on, it reads the driver's samples, one g down y and a turn
// about y; on Android 11 it has none.
//
// Last, a second pad, "Maul Stale DS4", comes while a stand-in for the
// library's Java helper's copies (test/android/java/maul/window/
// StaleCopies.java) reports it as Android 11 can leave an app's copy of a
// pad read while its kernel devices were being merged: a touchpad's
// pointer with no joystick ranges. The library must add it on its first
// record, its cross button's press, mapped by the keys the system says it
// has, with Android's standard gamepad axes, so that its left stick moved
// right reads right. The stand-in stands for the copy only: Android 11's
// own keeping of a stale copy, a race of a few milliseconds, is not made
// to happen, and the half second the library waits before it first reads
// a new device is not what keeps this pad out.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/gamepad.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/api-level.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Since the program began: the runner gives a test a minute, counted
// from before it installs it, so a timeout here comes first and names
// its phase.
#define DEADLINE_NS 45000000000ull
#define FILES       "/data/data/" MWIN_TEST_PACKAGE "/files/"
#define NAME        "Maul DS4"
#define STALE       "Maul Stale DS4"
#define OUTPUT      "/data/local/tmp/mwin-ds4.out"

// The USB DualShock 4 of CTS's sony_dualshock4_usb_register.json: its
// report descriptor, then the feature reports the drivers read as they
// bind: calibration (0x02), firmware (0xA3), the address hid-sony reads
// (0x81) and the one hid-playstation reads (0x12).
static const uint8_t s_descriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02, 0x09, 0x39, 0x15, 0x00, 0x25,
    0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x75, 0x06, 0x95, 0x01, 0x15, 0x00, 0x25, 0x7F, 0x81, 0x02, 0x05,
    0x01, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x21, 0x95, 0x36, 0x81, 0x02, 0x85, 0x05, 0x09, 0x22, 0x95, 0x1F, 0x91,
    0x02, 0x85, 0x04, 0x09, 0x23, 0x95, 0x24, 0xB1, 0x02, 0x85, 0x02, 0x09, 0x24, 0x95, 0x24, 0xB1,
    0x02, 0x85, 0x08, 0x09, 0x25, 0x95, 0x03, 0xB1, 0x02, 0x85, 0x10, 0x09, 0x26, 0x95, 0x04, 0xB1,
    0x02, 0x85, 0x11, 0x09, 0x27, 0x95, 0x02, 0xB1, 0x02, 0x85, 0x12, 0x06, 0x02, 0xFF, 0x09, 0x21,
    0x95, 0x0F, 0xB1, 0x02, 0x85, 0x13, 0x09, 0x22, 0x95, 0x16, 0xB1, 0x02, 0x85, 0x14, 0x06, 0x05,
    0xFF, 0x09, 0x20, 0x95, 0x10, 0xB1, 0x02, 0x85, 0x15, 0x09, 0x21, 0x95, 0x2C, 0xB1, 0x02, 0x06,
    0x80, 0xFF, 0x85, 0x80, 0x09, 0x20, 0x95, 0x06, 0xB1, 0x02, 0x85, 0x81, 0x09, 0x21, 0x95, 0x06,
    0xB1, 0x02, 0x85, 0x82, 0x09, 0x22, 0x95, 0x05, 0xB1, 0x02, 0x85, 0x83, 0x09, 0x23, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0x84, 0x09, 0x24, 0x95, 0x04, 0xB1, 0x02, 0x85, 0x85, 0x09, 0x25, 0x95, 0x06,
    0xB1, 0x02, 0x85, 0x86, 0x09, 0x26, 0x95, 0x06, 0xB1, 0x02, 0x85, 0x87, 0x09, 0x27, 0x95, 0x23,
    0xB1, 0x02, 0x85, 0x88, 0x09, 0x28, 0x95, 0x22, 0xB1, 0x02, 0x85, 0x89, 0x09, 0x29, 0x95, 0x02,
    0xB1, 0x02, 0x85, 0x90, 0x09, 0x30, 0x95, 0x05, 0xB1, 0x02, 0x85, 0x91, 0x09, 0x31, 0x95, 0x03,
    0xB1, 0x02, 0x85, 0x92, 0x09, 0x32, 0x95, 0x03, 0xB1, 0x02, 0x85, 0x93, 0x09, 0x33, 0x95, 0x0C,
    0xB1, 0x02, 0x85, 0xA0, 0x09, 0x40, 0x95, 0x06, 0xB1, 0x02, 0x85, 0xA1, 0x09, 0x41, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0xA2, 0x09, 0x42, 0x95, 0x01, 0xB1, 0x02, 0x85, 0xA3, 0x09, 0x43, 0x95, 0x30,
    0xB1, 0x02, 0x85, 0xA4, 0x09, 0x44, 0x95, 0x0D, 0xB1, 0x02, 0x85, 0xA5, 0x09, 0x45, 0x95, 0x15,
    0xB1, 0x02, 0x85, 0xA6, 0x09, 0x46, 0x95, 0x15, 0xB1, 0x02, 0x85, 0xF0, 0x09, 0x47, 0x95, 0x3F,
    0xB1, 0x02, 0x85, 0xF1, 0x09, 0x48, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0xF2, 0x09, 0x49, 0x95, 0x0F,
    0xB1, 0x02, 0x85, 0xA7, 0x09, 0x4A, 0x95, 0x01, 0xB1, 0x02, 0x85, 0xA8, 0x09, 0x4B, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0xA9, 0x09, 0x4C, 0x95, 0x08, 0xB1, 0x02, 0x85, 0xAA, 0x09, 0x4E, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0xAB, 0x09, 0x4F, 0x95, 0x39, 0xB1, 0x02, 0x85, 0xAC, 0x09, 0x50, 0x95, 0x39,
    0xB1, 0x02, 0x85, 0xAD, 0x09, 0x51, 0x95, 0x0B, 0xB1, 0x02, 0x85, 0xAE, 0x09, 0x52, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0xAF, 0x09, 0x53, 0x95, 0x02, 0xB1, 0x02, 0x85, 0xB0, 0x09, 0x54, 0x95, 0x3F,
    0xB1, 0x02, 0x85, 0xB1, 0x09, 0x55, 0x95, 0x02, 0xB1, 0x02, 0x85, 0xB2, 0x09, 0x56, 0x95, 0x02,
    0xB1, 0x02, 0xC0};
static const uint8_t s_feature02[] = {0x02, 0xF8, 0xFF, 0xFD, 0xFF, 0xF9, 0xFF, 0xDF, 0x21, 0xF1,
                                      0xDD, 0x95, 0x22, 0x67, 0xDD, 0xF2, 0x23, 0x1C, 0xDC, 0x1C,
                                      0x02, 0x1C, 0x02, 0xAA, 0x1F, 0x56, 0xE0, 0xF7, 0x20, 0x08,
                                      0xDF, 0x0A, 0x20, 0xF7, 0xDF, 0x06, 0x00};
static const uint8_t s_featureA3[] = {0xA3, 0x41, 0x70, 0x72, 0x20, 0x20, 0x38, 0x20, 0x32, 0x30,
                                      0x31, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x39, 0x3A,
                                      0x34, 0x36, 0x3A, 0x30, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x43, 0x03, 0x00, 0x00,
                                      0x00, 0x51, 0x00, 0x05, 0x00, 0x00, 0x80, 0x03, 0x00};
static const uint8_t s_feature12[] = {0x12, 0x62, 0x97, 0xC9, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t s_feature81[] = {0x81, 0x62, 0x97, 0xC9, 0x00, 0x00, 0x00};

typedef struct Program
{
    int phase;
    uint64_t beganNs;
    mwinWindowId window;
    mwinGamepadId pad;
    bool added;
    // Whether the pad has motion, as added or changed.
    bool motion;
    // When the copy the phase waits for was last asked for.
    uint64_t askedNs;
    // When the input service was seen listing the pad, 0 before.
    uint64_t listedNs;
    // The stale pad: whether it was added, its id, whether its first
    // record after that was its cross button's press, and how far right
    // its left stick went.
    bool staleAdded;
    mwinGamepadId stale;
    int staleFirst;
    float staleRight;
    // Whether the stand-in is in place; the ask for the stale pad, 1 once
    // done, -1 once failed, else 0; and the step of the ask that last did
    // not work, with what it answered.
    bool staleInstalled;
    int staleAsk;
    const char* staleStep;
    const char* staleAnswer;
} Program;

static char s_read[65536];

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void PutBytes(FILE* file, const uint8_t* bytes, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        fprintf(file, "%s%u", i == 0 ? "" : ",", (unsigned)bytes[i]);
    }
}

static void PutFeature(FILE* file, const uint8_t* bytes, size_t count, bool last)
{
    fprintf(file, "{\"id\":%u,\"data\":[", (unsigned)bytes[0]);
    PutBytes(file, bytes, count);
    fprintf(file, "]}%s", last ? "" : ",");
}

// The hid tool's input, a script run as the shell: the pad, then, once
// the driver has bound it, a pause that keeps it while the test runs.
// The hid tool reads its input as fast as it comes and answers the
// driver's feature report requests only after the delays read before
// them, so a delay read before the driver binds holds its answers back
// past the driver's timeout and the pad fails to bind: the script waits
// for the driver's battery to appear before the rest.
// An input report of the pad: centred, its sensors read as one g down y
// and a turn about y, its battery half full. The kernel passes an axis on
// only when its value changes by more than its fuzz (16 raw units for the
// sensors), so each report's readings differ from the last by 64.
static void PutReport(FILE* file, int index)
{
    uint8_t report[64] = {0x01, 0x80, 0x80, 0x80, 0x80, 0x08};
    // Gyroscope y, then accelerometer y, in the driver's raw units.
    uint16_t gyro = (uint16_t)(-1024 + index % 2 * 64);
    uint16_t acceleration = (uint16_t)(-8192 + index % 2 * 64);
    report[15] = (uint8_t)(gyro & 0xFF);
    report[16] = (uint8_t)(gyro >> 8);
    report[21] = (uint8_t)(acceleration & 0xFF);
    report[22] = (uint8_t)(acceleration >> 8);
    report[30] = 5;
    fprintf(file, "{\"id\":1,\"command\":\"report\",\"report\":[");
    PutBytes(file, report, sizeof(report));
    fprintf(file, "]}\n");
}

// The register of a pad: its name, and the last byte of its address,
// which the drivers make its unique id, so that two pads are two devices.
static void PutRegister(FILE* file, int id, const char* name, uint8_t address)
{
    uint8_t feature81[sizeof(s_feature81)];
    uint8_t feature12[sizeof(s_feature12)];
    memcpy(feature81, s_feature81, sizeof(feature81));
    memcpy(feature12, s_feature12, sizeof(feature12));
    feature81[1] = address;
    feature12[1] = address;
    fprintf(file,
            "{\"id\":%d,\"command\":\"register\",\"name\":\"%s\",\"vid\":1356,\"pid\":1476,"
            "\"bus\":\"usb\",\"descriptor\":[",
            id, name);
    PutBytes(file, s_descriptor, sizeof(s_descriptor));
    fprintf(file, "],\"feature_reports\":[");
    PutFeature(file, s_feature02, sizeof(s_feature02), false);
    PutFeature(file, s_featureA3, sizeof(s_featureA3), false);
    PutFeature(file, feature81, sizeof(feature81), false);
    PutFeature(file, feature12, sizeof(feature12), true);
    fprintf(file, "]}\n");
}

// An input report of the stale pad: its sticks centred but the left one
// right when right, its cross button (Android's A) held.
static void PutStaleReport(FILE* file, bool right)
{
    uint8_t report[64] = {0x01, right ? 0xFF : 0x80, 0x80, 0x80, 0x80, 0x28};
    fprintf(file, "{\"id\":2,\"command\":\"report\",\"report\":[");
    PutBytes(file, report, sizeof(report));
    fprintf(file, "]}\n");
}

// Waits, within a bound, until a count of the drivers' batteries.
static void PutBatteryWait(FILE* script, int count)
{
    fprintf(script,
            "i=0\n"
            "while [ $i -lt 300 ] && [ \"$(ls /sys/class/power_supply | "
            "grep -cE 'sony_controller|ps-controller')\" -lt %d ]; do\n"
            "    sleep 0.1\n"
            "    i=$((i + 1))\n"
            "done\n",
            count);
}

// The hid tool's input, a script run as the shell: the pad, then, once
// the driver has bound it, a report every tenth of a second for a
// minute, which keeps it while the test runs; and
// once the test asks for it (the file ds4-stale), the stale pad, then,
// once bound and its node open in Android's input hub, its cross button
// pressed, its left stick right, and a pause. The hid tool reads its input as fast as it comes and
// answers the driver's feature report requests only after the delays read before them for that
// device, so a delay read before the driver binds holds its answers back past the driver's timeout
// and the pad fails to bind: the script waits for the driver's battery to appear before the rest.
static bool WriteScript(void)
{
    const char* const names[5] = {"ds4-add.json", "ds4-rest.json", "ds4-stale-add.json",
                                  "ds4-stale-rest.json", "ds4.sh"};
    FILE* files[5] = {nullptr};
    bool opened = true;
    for (int i = 0; i < 5; i++)
    {
        char path[128];
        (void)snprintf(path, sizeof(path), FILES "%s", names[i]);
        files[i] = fopen(path, "w");
        opened = opened && files[i] != nullptr;
    }
    if (opened)
    {
        PutRegister(files[0], 1, NAME, 0x62);
        for (int i = 0; i < 600; i++)
        {
            PutReport(files[1], i);
            fprintf(files[1], "{\"id\":1,\"command\":\"delay\",\"duration\":100}\n");
        }
        PutRegister(files[2], 2, STALE, 0x63);
        fprintf(files[3], "{\"id\":2,\"command\":\"delay\",\"duration\":300}\n");
        PutStaleReport(files[3], false);
        fprintf(files[3], "{\"id\":2,\"command\":\"delay\",\"duration\":300}\n");
        PutStaleReport(files[3], true);
        fprintf(files[3], "{\"id\":2,\"command\":\"delay\",\"duration\":60000}\n");
        FILE* script = files[4];
        fprintf(script, "(run-as " MWIN_TEST_PACKAGE " cat files/ds4-add.json\n");
        PutBatteryWait(script, 1);
        fprintf(script, "run-as " MWIN_TEST_PACKAGE " cat files/ds4-rest.json\n"
                        "i=0\n"
                        "while [ $i -lt 600 ] && ! run-as " MWIN_TEST_PACKAGE
                        " ls files/ds4-stale > /dev/null 2>&1; do\n"
                        "    sleep 0.1\n"
                        "    i=$((i + 1))\n"
                        "done\n"
                        "run-as " MWIN_TEST_PACKAGE " cat files/ds4-stale-add.json\n");
        PutBatteryWait(script, 2);
        // Android reads a device's reports only once its input hub has
        // opened the device's node: until then the press would be lost.
        fprintf(script, "i=0\n"
                        "while [ $i -lt 300 ] && ! dumpsys input | "
                        "grep -qE '^ +[0-9]+: " STALE "$'; do\n"
                        "    sleep 0.1\n"
                        "    i=$((i + 1))\n"
                        "done\n");
        fprintf(script, "run-as " MWIN_TEST_PACKAGE " cat files/ds4-stale-rest.json) | hid -\n");
    }
    bool closed = true;
    for (int i = 0; i < 5; i++)
    {
        closed = (files[i] == nullptr || fclose(files[i]) == 0) && closed;
    }
    return opened && closed;
}

// Asks the runner to copy the hid tool's output and the input service's
// vibrating lines into a file of the test's, whole once it is there.
static void AskCopy(const char* name)
{
    printf("adb: sh -c '{ cat " OUTPUT
           "; echo; dumpsys input | grep Vibrating; } | run-as " MWIN_TEST_PACKAGE
           " sh -c \"cat > files/%s.part && mv files/%s.part files/%s\"'\n",
           name, name, name);
}

// The copy, once there: NULL before.
static const char* ReadCopy(const char* name)
{
    char path[128];
    (void)snprintf(path, sizeof(path), FILES "%s", name);
    FILE* file = fopen(path, "r");
    if (file == nullptr)
    {
        return nullptr;
    }
    size_t count = fread(s_read, 1, sizeof(s_read) - 1, file);
    (void)fclose(file);
    s_read[count] = '\0';
    return s_read;
}

// The motors of the last output report that sets them (report 5, its
// first flag), light then heavy: false when there is none.
static bool LastMotors(const char* text, unsigned* light, unsigned* heavy)
{
    bool found = false;
    for (const char* at = strstr(text, "\"reportData\":["); at != nullptr;
         at = strstr(at + 1, "\"reportData\":["))
    {
        unsigned bytes[6];
        const char* next = at + strlen("\"reportData\":[");
        int count = 0;
        while (count < 6)
        {
            char* end = nullptr;
            unsigned long value = strtoul(next, &end, 10);
            if (end == next)
            {
                break;
            }
            bytes[count++] = (unsigned)value;
            next = *end == ',' ? end + 1 : end;
        }
        if (count == 6 && bytes[0] == 5 && (bytes[1] & 1u) != 0)
        {
            *light = bytes[4];
            *heavy = bytes[5];
            found = true;
        }
    }
    return found;
}

static void Paint(const Program* program, mwinContext* context)
{
    mwinNativeHandles handles = {0};
    if (mwinGetNativeHandles(context, program->window, &handles) != mwin_success)
    {
        return;
    }
    ANativeWindow* window = handles.handles.android.window;
    ANativeWindow_Buffer buffer;
    (void)ANativeWindow_setBuffersGeometry(window, 0, 0, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    if (ANativeWindow_lock(window, &buffer, nullptr) == 0)
    {
        for (int32_t row = 0; row < buffer.height; row++)
        {
            memset((uint8_t*)buffer.bits + (size_t)row * (size_t)buffer.stride * 4u, 0x40,
                   (size_t)buffer.width * 4u);
        }
        (void)ANativeWindow_unlockAndPost(window);
    }
}

// The pad added: Android names the device it merges from the driver's
// three (the pad, its touchpad and its motion sensors) after one of them.
static void Added(Program* program, mwinContext* context, mwinGamepadId id)
{
    mwinGamepadInfo info = {0};
    if (mwinGetGamepadInfo(context, id, &info) != mwin_success || info.nameLength < strlen(NAME) ||
        memcmp(info.name, NAME, strlen(NAME)) != 0)
    {
        return;
    }
    program->pad = id;
    program->added = true;
    program->motion = (info.capabilities & mwin_padMotion) != 0;
    CHECK(info.vendor == 0x054c && info.product == 0x05c4 && info.mapped,
          "the pad's vendor and product, mapped");
    CHECK((info.capabilities & mwin_padRumble) != 0, "the pad's motors");
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventGamepadAdded || event.type == mwin_eventGamepadChanged ||
            event.type == mwin_eventGamepadRemoved)
        {
            // For a timeout's report: what came, when.
            mwinGamepadInfo info = {0};
            (void)mwinGetGamepadInfo(context, event.data.gamepad, &info);
            printf("  %.2f s: gamepad %s: %.*s, capabilities %u\n",
                   (double)(NowNs() - program->beganNs) / 1e9,
                   event.type == mwin_eventGamepadAdded     ? "added"
                   : event.type == mwin_eventGamepadChanged ? "changed"
                                                            : "removed",
                   (int)info.nameLength, info.name, info.capabilities);
        }
        if (event.type == mwin_eventGamepadAdded && !program->added)
        {
            Added(program, context, event.data.gamepad);
        }
        else if (event.type == mwin_eventGamepadChanged && program->added &&
                 event.data.gamepad.index1 == program->pad.index1 &&
                 event.data.gamepad.generation == program->pad.generation)
        {
            mwinGamepadInfo info = {0};
            (void)mwinGetGamepadInfo(context, program->pad, &info);
            program->motion = (info.capabilities & mwin_padMotion) != 0;
        }
        else if (event.type == mwin_eventGamepadAdded && !program->staleAdded)
        {
            mwinGamepadInfo info = {0};
            program->staleAdded =
                mwinGetGamepadInfo(context, event.data.gamepad, &info) == mwin_success &&
                info.nameLength >= strlen(STALE) && memcmp(info.name, STALE, strlen(STALE)) == 0;
            program->stale = event.data.gamepad;
        }
        else if (program->staleAdded && program->staleFirst == 0 &&
                 (event.type == mwin_eventGamepadButtonDown ||
                  event.type == mwin_eventGamepadButtonUp ||
                  event.type == mwin_eventGamepadAxisMoved))
        {
            // The pad's first record: its press, 1, or anything else, -1.
            mwinGamepadId id = event.type == mwin_eventGamepadAxisMoved
                                   ? event.data.gamepadAxis.gamepad
                                   : event.data.gamepadButton.gamepad;
            bool press = event.type == mwin_eventGamepadButtonDown &&
                         event.data.gamepadButton.button == mwin_padFaceSouth;
            if (id.index1 == program->stale.index1 && id.generation == program->stale.generation)
            {
                program->staleFirst = press ? 1 : -1;
            }
        }
        if (program->staleAdded && event.type == mwin_eventGamepadAxisMoved &&
            event.data.gamepadAxis.gamepad.index1 == program->stale.index1 &&
            event.data.gamepadAxis.axis == mwin_padStickLeftX &&
            event.data.gamepadAxis.value > program->staleRight)
        {
            program->staleRight = event.data.gamepadAxis.value;
        }
    }
}

// A class of the test's application, through its class loader.
static jclass LoadClass(JNIEnv* env, ANativeActivity* activity, const char* name)
{
    jclass activities = (*env)->GetObjectClass(env, activity->clazz);
    jobject loader = (*env)->CallObjectMethod(
        env, activity->clazz,
        (*env)->GetMethodID(env, activities, "getClassLoader", "()Ljava/lang/ClassLoader;"));
    jclass loaders = (*env)->GetObjectClass(env, loader);
    jstring text = (*env)->NewStringUTF(env, name);
    jclass type = (*env)->CallObjectMethod(
        env, loader,
        (*env)->GetMethodID(env, loaders, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"),
        text);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, text);
    (*env)->DeleteLocalRef(env, loaders);
    (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, activities);
    return type;
}

// Puts the stand-in for the helper's copies in place (StaleCopies.java),
// then asks the hid script for the stale pad: 1 once both are done; 0
// while the window has no surface, when the library answers its handles
// with mwin_errorState until a record brings the surface back; -1 for
// any other failure. The step that did not work is kept with its answer.
static int AskStale(Program* program, mwinContext* context)
{
    if (!program->staleInstalled)
    {
        mwinNativeHandles handles = {0};
        mwinResult result = mwinGetNativeHandles(context, program->window, &handles);
        if (result != mwin_success)
        {
            program->staleStep = "the window's handles";
            program->staleAnswer = mwinResultName(result);
            return result == mwin_errorState ? 0 : -1;
        }
        ANativeActivity* activity = handles.handles.android.activity;
        JNIEnv* env = activity->env;
        jclass stand = LoadClass(env, activity, "maul.window.StaleCopies");
        if (stand == nullptr)
        {
            program->staleStep = "the stand-in's class";
            program->staleAnswer = "not found";
            return -1;
        }
        (*env)->CallStaticVoidMethod(env, stand,
                                     (*env)->GetStaticMethodID(env, stand, "install", "()V"));
        program->staleInstalled = !(*env)->ExceptionCheck(env);
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, stand);
        if (!program->staleInstalled)
        {
            program->staleStep = "the stand-in's install";
            program->staleAnswer = "an exception";
            return -1;
        }
    }
    FILE* flag = fopen(FILES "ds4-stale", "w");
    if (flag == nullptr || fclose(flag) != 0)
    {
        program->staleStep = "the flag file";
        program->staleAnswer = "not written";
        return -1;
    }
    return 1;
}

// The ask for the stale pad done, else the step that did not work.
static void CheckAsked(const Program* program)
{
    CHECK(program->staleAsk == 1, "the stand-in in place and the stale pad asked for");
    if (program->staleAsk != 1)
    {
        printf("  the stale pad not asked for: %s answered %s\n",
               program->staleStep != nullptr ? program->staleStep : "no step",
               program->staleAnswer != nullptr ? program->staleAnswer : "nothing");
    }
}

// The names of the copies phases 2 and 3 wait for.
static const char* const s_copies[2] = {"ds4-running", "ds4-stopped"};

// Whether a copy shows what its phase asked for: from Android 12 the
// last report's motors, light in full and heavy at a quarter, then both
// stopped; on Android 11 the pad vibrating, then not.
static bool Shows(int phase, const char* text)
{
    unsigned light = 0;
    unsigned heavy = 0;
    if (android_get_device_api_level() >= 31)
    {
        bool motors = LastMotors(text, &light, &heavy);
        return phase == 2 ? motors && light == 255 && heavy == 64
                          : motors && light == 0 && heavy == 0;
    }
    bool vibrating = strstr(text, "Vibrating: true") != nullptr;
    return phase == 2 ? vibrating : !vibrating;
}

// Asks for a copy, again each second until one shows what the phase
// asked for: the driver's report and the input service's state follow
// the call a moment later.
static bool Copied(Program* program)
{
    const char* name = s_copies[program->phase - 2];
    const char* text = ReadCopy(name);
    if (text != nullptr && Shows(program->phase, text))
    {
        return true;
    }
    uint64_t now = NowNs();
    if (now - program->askedNs >= 1000000000u)
    {
        char path[128];
        (void)snprintf(path, sizeof(path), FILES "%s", name);
        (void)remove(path);
        AskCopy(name);
        program->askedNs = now;
    }
    return false;
}

// Whether the input service lists the pad, asked again each second,
// so that a pad never added says whether Android had it.
static bool Listed(Program* program)
{
    const char* text = ReadCopy("ds4-listed");
    if (text != nullptr && strstr(text, NAME) != nullptr)
    {
        program->listedNs = NowNs();
        printf("  %.2f s: listed by the input service\n",
               (double)(program->listedNs - program->beganNs) / 1e9);
        return true;
    }
    uint64_t now = NowNs();
    if (now - program->askedNs >= 1000000000u)
    {
        (void)remove(FILES "ds4-listed");
        printf("adb: sh -c 'dumpsys input | grep -E \"Device [0-9]+: " NAME
               "\" | run-as " MWIN_TEST_PACKAGE
               " sh -c \"cat > files/ds4-listed.part && mv files/ds4-listed.part "
               "files/ds4-listed\"'\n");
        program->askedNs = now;
    }
    return false;
}

static bool Ready(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
    {
        mwinWindowState state = {0};
        (void)mwinGetWindowState(context, program->window, &state);
        return state.focused;
    }
    case 1:
        return (program->listedNs != 0 || Listed(program)) && program->added;
    case 2:
    case 3:
        return Copied(program);
    case 4:
        return android_get_device_api_level() < 31 || program->motion;
    case 5:
    {
        // Each sensor's samples come on their own: both must have come.
        mwinGamepadMotion motion = {0};
        return android_get_device_api_level() < 31 ||
               (mwinGetGamepadMotion(context, program->pad, &motion) == mwin_success &&
                motion.acceleration[1] != 0.0f && motion.rotationRate[1] != 0.0f);
    }
    case 6:
        // Asked for at each frame while the window has no surface, the one
        // failure the library documents as passing; any other ends the
        // phase at once.
        if (program->staleAsk == 0)
        {
            program->staleAsk = AskStale(program, context);
            return program->staleAsk < 0;
        }
        return program->staleAsk < 0 || (program->staleFirst != 0 && program->staleRight > 0.99f);
    default:
        return true;
    }
}

// The pad's motion: one g down y as the driver calibrates it, and a
// turn about y, nothing about the other axes.
static void CheckMotion(Program* program, mwinContext* context)
{
    mwinGamepadMotion motion = {0};
    CHECK(mwinGetGamepadMotion(context, program->pad, &motion) == mwin_success &&
              fabsf(motion.acceleration[0]) < 0.1f && fabsf(motion.acceleration[1] + 9.8f) < 1.0f &&
              fabsf(motion.acceleration[2]) < 0.1f,
          "one g down y");
    CHECK(fabsf(motion.rotationRate[0]) < 0.1f && motion.rotationRate[1] < -0.5f &&
              fabsf(motion.rotationRate[2]) < 0.1f,
          "a turn about y");
    printf("  acceleration %.3f %.3f %.3f, rotation rate %.3f %.3f %.3f\n",
           (double)motion.acceleration[0], (double)motion.acceleration[1],
           (double)motion.acceleration[2], (double)motion.rotationRate[0],
           (double)motion.rotationRate[1], (double)motion.rotationRate[2]);
}

// The stale pad, which the stand-in reports as a touchpad's pointer with
// no joystick ranges: added on its first record, its press, mapped by the
// keys the system says it has, and its left stick on Android's standard
// X axis.
static void CheckStale(const Program* program, mwinContext* context)
{
    CHECK(program->staleAdded && program->staleFirst == 1,
          "the stale pad added by its press, its first record");
    mwinGamepadInfo info = {0};
    mwinGamepadState state = {0};
    CHECK(mwinGetGamepadInfo(context, program->stale, &info) == mwin_success && info.mapped,
          "the stale pad mapped by its keys");
    CHECK(mwinGetGamepadState(context, program->stale, &state) == mwin_success &&
              (state.buttons & 1u << mwin_padFaceSouth) != 0 &&
              state.axes[mwin_padStickLeftX] > 0.99f,
          "its cross held and its left stick right, on the standard axes");
}

// The check of what the running or the stopped motors should show, on
// the copy read.
static void CheckCopy(int phase)
{
    bool reports = android_get_device_api_level() >= 31;
    bool shown = Shows(phase, s_read);
    if (phase == 2)
    {
        CHECK(shown, reports ? "the light motor in full and the heavy one at a quarter"
                             : "the pad vibrating");
    }
    else
    {
        CHECK(shown, reports ? "both motors stopped" : "the pad still");
    }
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
        Paint(program, context);
        CHECK(WriteScript(), "the hid script written");
        printf("adb: sh -c 'nohup sh -c \"$(run-as " MWIN_TEST_PACKAGE
               " cat files/ds4.sh)\" > " OUTPUT " 2>&1 &'\n");
        break;
    case 1:
        // Longer than the test runs: only the stop ends it.
        CHECK(mwinSetGamepadRumble(context, program->pad, 0.25f, 1.0f, 60000) == mwin_success,
              "the rumble asked for");
        break;
    case 2:
        CheckCopy(program->phase);
        CHECK(mwinSetGamepadRumble(context, program->pad, 0.0f, 0.0f, 0) == mwin_success,
              "the rumble stopped");
        break;
    case 3:
        CheckCopy(program->phase);
        break;
    case 4:
        if (android_get_device_api_level() >= 31)
        {
            CHECK(mwinSetGamepadMotion(context, program->pad, true) == mwin_success,
                  "the pad's motion turned on");
        }
        else
        {
            CHECK(!program->motion &&
                      mwinSetGamepadMotion(context, program->pad, true) == mwin_errorUnsupported,
                  "no motion on Android 11");
        }
        break;
    case 5:
        if (android_get_device_api_level() >= 31)
        {
            CheckMotion(program, context);
            CHECK(mwinSetGamepadMotion(context, program->pad, false) == mwin_success,
                  "the pad's motion turned off");
        }
        break;
    case 6:
        CheckAsked(program);
        if (program->staleAsk == 1)
        {
            CheckStale(program, context);
        }
        break;
    default:
        break;
    }
    program->phase += 1;
    program->askedNs = 0;
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->beganNs = NowNs();
    // Copies and the flag a run before this one may have left.
    (void)remove(FILES "ds4-stale");
    (void)remove(FILES "ds4-listed");
    (void)remove(FILES "ds4-running");
    (void)remove(FILES "ds4-stopped");
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (program->phase > 6)
    {
        return mwin_frameStop;
    }
    if (Ready(program, context))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->beganNs > DEADLINE_NS)
    {
        printf("timed out in phase %d, the pad %s; ", program->phase,
               program->added ? "added" : "not added");
        if (program->listedNs != 0)
        {
            printf("Android listed it at %.2f s\n",
                   (double)(program->listedNs - program->beganNs) / 1e9);
        }
        else
        {
            printf("Android had not listed it\n");
        }
        if (program->phase == 2 || program->phase == 3)
        {
            CheckCopy(program->phase);
        }
        CHECK(program->phase != 4, "the pad's motion, as added or changed");
        if (program->phase == 5)
        {
            CheckMotion(program, context);
        }
        if (program->phase == 6)
        {
            CheckAsked(program);
            printf("  the stale pad %s, its first record %s, its left stick at most %.2f\n",
                   program->staleAdded ? "added" : "not added",
                   program->staleFirst == 0   ? "not come"
                   : program->staleFirst == 1 ? "its press"
                                              : "another",
                   (double)program->staleRight);
            CheckStale(program, context);
        }
        s_failures += 1;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase > 6, "every phase ran");
    printf("result: %d failures\n", s_failures);
}

mwinAppDef mwinAndroidMain(void)
{
    static Program program;
    // The runner reads the file with run-as.
    if (freopen(FILES "out", "w", stdout) != nullptr)
    {
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    return def;
}
