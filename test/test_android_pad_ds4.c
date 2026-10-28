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
// input service says the pad vibrates, then that it does not.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/gamepad.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/api-level.h>
#include <android/native_window.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Since the program began: the runner gives a test a minute, counted
// from before it installs it, so a timeout here comes first and names
// its phase.
#define DEADLINE_NS 45000000000ull
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
    // When the copy the phase waits for was last asked for.
    uint64_t askedNs;
    // When the input service was seen listing the pad, 0 before.
    uint64_t listedNs;
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
    fprintf(rest, "{\"id\":1,\"command\":\"delay\",\"duration\":60000}\n");
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
    default:
        return true;
    }
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
    (void)remove(FILES "ds4-listed");
    (void)remove(FILES "ds4-running");
    (void)remove(FILES "ds4-stopped");
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (program->phase > 3)
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
        if (program->phase >= 2)
        {
            CheckCopy(program->phase);
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
    CHECK(program->phase > 3, "every phase ran");
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
