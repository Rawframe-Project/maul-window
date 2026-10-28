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
// about y; on Android 11 it has none. From Android 12 its battery is read
// from its reports and a change is told: 55% (half full, unplugged, as
// the drivers count it), then 85%; on Android 11 it has none. Last, the
// program quits with the pad rumbling and its motion on, and once the
// context is gone the motors are stopped and the sensors off, as
// Android reads them.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/gamepad.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/api-level.h>
#include <android/native_window.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Since the program began: the runner gives a test a minute, counted
// from before it installs it, so a timeout here comes first and names
// its phase.
#define DEADLINE_NS 45000000000ull
// Since the program quit: Android freezes a process about ten seconds
// after its activity has gone (the cached apps freezer), so the wait
// for the stopped pad ends before that and the result line comes out.
#define GONE_NS 5000000000ull
#define FILES       "/data/data/" MWIN_TEST_PACKAGE "/files/"
#define NAME        "Maul DS4"
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
    // The pad's batteries, as added or changed: 55% then 85%, or any.
    bool half;
    bool fuller;
    bool battery;
    // When the copy the phase waits for was last asked for.
    uint64_t askedNs;
    // When the input service was seen listing the pad, 0 before.
    uint64_t listedNs;
    uint64_t quitNs;
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

// An input report of the pad: centred, its sensors read as one g down y
// and a turn about y, its battery at level 5 of 10, from the hundredth
// report at level 8, unplugged. The kernel passes an axis on
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
    report[30] = index < 100 ? 5 : 8;
    fprintf(file, "{\"id\":1,\"command\":\"report\",\"report\":[");
    PutBytes(file, report, sizeof(report));
    fprintf(file, "]}\n");
}

// The hid tool's input, a script run as the shell: the pad, then, once
// the driver has bound it, a report every tenth of a second for a
// minute, which keeps it while the test runs.
// The hid tool reads its input as fast as it comes and answers the
// driver's feature report requests only after the delays read before
// them, so a delay read before the driver binds holds its answers back
// past the driver's timeout and the pad fails to bind: the script waits
// for the driver's battery to appear before the rest.
static bool WriteScript(void)
{
    FILE* add = fopen(FILES "ds4-add.json", "w");
    FILE* rest = fopen(FILES "ds4-rest.json", "w");
    FILE* script = fopen(FILES "ds4.sh", "w");
    if (add == nullptr || rest == nullptr || script == nullptr)
    {
        if (add != nullptr)
        {
            (void)fclose(add);
        }
        if (rest != nullptr)
        {
            (void)fclose(rest);
        }
        if (script != nullptr)
        {
            (void)fclose(script);
        }
        return false;
    }
    fprintf(add,
            "{\"id\":1,\"command\":\"register\",\"name\":\"" NAME "\",\"vid\":1356,\"pid\":1476,"
            "\"bus\":\"usb\",\"descriptor\":[");
    PutBytes(add, s_descriptor, sizeof(s_descriptor));
    fprintf(add, "],\"feature_reports\":[");
    PutFeature(add, s_feature02, sizeof(s_feature02), false);
    PutFeature(add, s_featureA3, sizeof(s_featureA3), false);
    PutFeature(add, s_feature81, sizeof(s_feature81), false);
    PutFeature(add, s_feature12, sizeof(s_feature12), true);
    fprintf(add, "]}\n");
    for (int i = 0; i < 600; i++)
    {
        PutReport(rest, i);
        fprintf(rest, "{\"id\":1,\"command\":\"delay\",\"duration\":100}\n");
    }
    fprintf(script, "(run-as " MWIN_TEST_PACKAGE " cat files/ds4-add.json\n"
                    "i=0\n"
                    "while [ $i -lt 300 ] && ! ls /sys/class/power_supply | "
                    "grep -qE 'sony_controller|ps-controller'; do\n"
                    "    sleep 0.1\n"
                    "    i=$((i + 1))\n"
                    "done\n"
                    "run-as " MWIN_TEST_PACKAGE " cat files/ds4-rest.json) | hid -\n");
    bool closed = fclose(rest) == 0;
    closed = fclose(script) == 0 && closed;
    return fclose(add) == 0 && closed;
}

// Asks the runner to copy the hid tool's output and the input service's
// vibrating lines into a file of the test's, whole once it is there.
static void AskCopy(const char* name)
{
    printf(
        "adb: sh -c '{ cat " OUTPUT
        "; echo; dumpsys input | grep -E \"Vibrating|enabled: [01]\"; } | run-as " MWIN_TEST_PACKAGE
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

// A battery of the pad told: 85% counts only after 55%.
static void Battery(Program* program, int8_t percent)
{
    program->battery = program->battery || percent != -1;
    program->half = program->half || percent == 55;
    program->fuller = program->fuller || (program->half && percent == 85);
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
    Battery(program, info.battery);
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
            Battery(program, info.battery);
        }
    }
}

// What a copy should show: from Android 12 the last report's motors and
// whether the pad's sensors are on; on Android 11 whether it vibrates.
typedef struct Want
{
    const char* name;
    unsigned light;
    unsigned heavy;
    bool sensing;
    // The check's words, from Android 12 and on Android 11.
    const char* check;
    const char* check11;
} Want;

// What phases 2, 3 and 7 and the end wait for: the light motor in full
// and the heavy one at a quarter, both stopped, both at half with the
// sensors on, and everything stopped once the context is gone.
static const Want s_wants[4] = {
    {"ds4-running", 255, 64, false, "the light motor in full and the heavy one at a quarter",
     "the pad vibrating"},
    {"ds4-stopped", 0, 0, false, "both motors stopped", "the pad still"},
    {"ds4-quitting", 128, 128, true, "before quitting, both motors at half and the sensors on",
     "before quitting, the pad vibrating"},
    {"ds4-gone", 0, 0, false, "once the context is gone, the motors stopped and the sensors off",
     "once the context is gone, the pad still"},
};

static bool Shows(const Want* want, const char* text)
{
    bool running = want->light != 0 || want->heavy != 0;
    if (android_get_device_api_level() >= 31)
    {
        unsigned light = 0;
        unsigned heavy = 0;
        bool sensing = strstr(text, "enabled: 1") != nullptr;
        return LastMotors(text, &light, &heavy) && light == want->light && heavy == want->heavy &&
               sensing == want->sensing;
    }
    return (strstr(text, "Vibrating: true") != nullptr) == running;
}

// Asks for a copy, again each second until one shows what is wanted:
// the driver's report and the input service's state follow the call a
// moment later.
static bool Copied(const Want* want, uint64_t* askedNs)
{
    const char* text = ReadCopy(want->name);
    if (text != nullptr && Shows(want, text))
    {
        return true;
    }
    uint64_t now = NowNs();
    if (now - *askedNs >= 1000000000u)
    {
        char path[128];
        (void)snprintf(path, sizeof(path), FILES "%s", want->name);
        (void)remove(path);
        AskCopy(want->name);
        *askedNs = now;
    }
    return false;
}

// The want of phase 2, 3 or 7.
static const Want* WantOf(int phase)
{
    return &s_wants[phase == 7 ? 2 : phase - 2];
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
    case 7:
        return Copied(WantOf(program->phase), &program->askedNs);
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
        return android_get_device_api_level() < 31 || program->fuller;
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

// The check of what is wanted, on the copy read, and what the copy
// showed when it fails.
static void CheckCopy(const Want* want)
{
    bool shows = Shows(want, s_read);
    CHECK(shows, android_get_device_api_level() >= 31 ? want->check : want->check11);
    if (!shows)
    {
        unsigned light = 0;
        unsigned heavy = 0;
        bool found = LastMotors(s_read, &light, &heavy);
        printf("  the copy: motors %s %u %u, sensors %s, vibrating %s\n",
               found ? "set to" : "never set, at", light, heavy,
               strstr(s_read, "enabled: 1") != nullptr ? "on" : "off",
               strstr(s_read, "Vibrating: true") != nullptr ? "true" : "false");
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
        CheckCopy(WantOf(program->phase));
        CHECK(mwinSetGamepadRumble(context, program->pad, 0.0f, 0.0f, 0) == mwin_success,
              "the rumble stopped");
        break;
    case 3:
        CheckCopy(WantOf(program->phase));
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
        if (android_get_device_api_level() >= 31)
        {
            CHECK(program->fuller, "the battery's change told, 55% then 85%");
        }
        else
        {
            CHECK(!program->battery, "no battery on Android 11");
        }
        // The program quits with both running.
        CHECK(mwinSetGamepadRumble(context, program->pad, 0.5f, 0.5f, 60000) == mwin_success,
              "the rumble asked for, to quit with");
        if (android_get_device_api_level() >= 31)
        {
            CHECK(mwinSetGamepadMotion(context, program->pad, true) == mwin_success,
                  "the pad's motion turned on, to quit with");
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
    // Copies a run before this one may have left.
    for (size_t i = 0; i < sizeof(s_wants) / sizeof(s_wants[0]); i++)
    {
        char path[128];
        (void)snprintf(path, sizeof(path), FILES "%s", s_wants[i].name);
        (void)remove(path);
    }
    (void)remove(FILES "ds4-listed");
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (program->phase > 7)
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
        if (program->phase == 2 || program->phase == 3 || program->phase == 7)
        {
            CheckCopy(WantOf(program->phase));
        }
        CHECK(program->phase != 6, "the battery's change told, 55% then 85%");
        CHECK(program->phase != 4, "the pad's motion, as added or changed");
        if (program->phase == 5)
        {
            CheckMotion(program, context);
        }
        s_failures += 1;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

// After the program quits, once the context is gone: the motors
// stopped and the sensors off, then the result.
static void* Gone(void* user)
{
    const Program* program = user;
    const Want* want = &s_wants[3];
    uint64_t askedNs = 0;
    while (!Copied(want, &askedNs) && NowNs() - program->quitNs < GONE_NS)
    {
        // hygiene: waits for the runner's copy, ended by the deadline
        (void)nanosleep(&(struct timespec){.tv_nsec = 100000000}, nullptr);
    }
    CheckCopy(want);
    printf("result: %d failures\n", s_failures);
    return nullptr;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase > 7, "every phase ran");
    // The context stops the pads after this returns: a thread waits for
    // that, and the runner for the result it writes.
    program->quitNs = NowNs();
    pthread_t thread;
    if (program->phase <= 7 || pthread_create(&thread, nullptr, Gone, program) != 0 ||
        pthread_detach(thread) != 0)
    {
        printf("result: %d failures\n", s_failures);
    }
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
