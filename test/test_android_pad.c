// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads on Android, in the emulator (tools/run_android_app.sh), with
// three USB HID devices Android's hid tool makes through the kernel's
// uhid, as a real one comes: an Xbox 360 pad's ids (Android's own layout
// for it: the right stick on Z and RZ, the triggers on LTRIGGER and
// RTRIGGER), a gamepad Android has no layout for (the generic one: the
// right stick on RX and RY, the triggers on Z and RZ, a hat), and a
// joystick with no gamepad buttons, which comes raw. Each is added with
// its name, ids and kind and no motors; its buttons and axes come as
// records and as its state; each is removed when its tool ends, the
// joystick's (a tool of its own) first, while the pads are still there.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/gamepad.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_window.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define FILES       "/data/data/" MWIN_TEST_PACKAGE "/files/"
#define PADS        3

// The pads by the hid tool's ids, 1 to 3.
static const char* const s_names[PADS] = {"Maul Xbox Pad", "Maul Generic Pad", "Maul Joystick"};
static const uint16_t s_products[PADS] = {0x028e, 0x5a01, 0x5a02};
static const uint16_t s_vendors[PADS] = {0x045e, 0x1209, 0x1209};

// A gamepad's report: 16 buttons, X Y RX RY from -32768 to 32767, Z and
// RZ from 0 to 255, a hat from 0 (up) clockwise to 7, 8 for none.
static const uint8_t s_gamepad[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25,
    0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33,
    0x09, 0x34, 0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02, 0x09,
    0x32, 0x09, 0x35, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0x09,
    0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95,
    0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01, 0x81, 0x03, 0xC0};

// A joystick's report: 8 buttons, X and Y from 0 to 255.
static const uint8_t s_joystick[] = {0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x05, 0x09, 0x19, 0x01,
                                     0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,
                                     0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00,
                                     0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0xC0};

typedef struct Pad
{
    mwinGamepadId id;
    bool added;
    bool removed;
    uint32_t downs;
    uint32_t ups;
    float lowest[MWIN_GAMEPAD_RAW_AXES];
    float highest[MWIN_GAMEPAD_RAW_AXES];
    bool raw;
} Pad;

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    Pad pads[PADS];
    // When every release was seen: the axes' motion is an input event of
    // its own, behind the keys', and the script pauses long after both.
    uint64_t releasedNs;
    // When the joystick went, and when the first pad went after it.
    uint64_t joystickGoneNs;
    uint64_t padGoneNs;
} Program;

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

// A gamepad's report: buttons, the four 16-bit axes, Z, RZ and the hat.
static void PutReport(FILE* file, int id, uint16_t buttons, const int16_t* sticks, uint8_t z,
                      uint8_t rz, uint8_t hat)
{
    uint8_t report[13] = {(uint8_t)buttons, (uint8_t)(buttons >> 8)};
    for (int i = 0; i < 4; i++)
    {
        report[2 + 2 * i] = (uint8_t)((uint16_t)sticks[i] & 0xFF);
        report[3 + 2 * i] = (uint8_t)((uint16_t)sticks[i] >> 8);
    }
    report[10] = z;
    report[11] = rz;
    report[12] = hat;
    fprintf(file, "{\"id\":%d,\"command\":\"report\",\"report\":[", id);
    PutBytes(file, report, sizeof(report));
    fprintf(file, "]}\n");
}

// A pause of the devices first to last: each device has its own queue,
// which a delay holds back.
static void PutDelay(FILE* file, int first, int last, int milliseconds)
{
    for (int id = first; id <= last; id++)
    {
        fprintf(file, "{\"id\":%d,\"command\":\"delay\",\"duration\":%d}\n", id, milliseconds);
    }
}

static void PutRegister(FILE* file, int index)
{
    fprintf(file,
            "{\"id\":%d,\"command\":\"register\",\"name\":\"%s\",\"vid\":%u,\"pid\":%u,"
            "\"bus\":\"usb\",\"descriptor\":[",
            index + 1, s_names[index], s_vendors[index], s_products[index]);
    if (index < 2)
    {
        PutBytes(file, s_gamepad, sizeof(s_gamepad));
    }
    else
    {
        PutBytes(file, s_joystick, sizeof(s_joystick));
    }
    fprintf(file, "]}\n");
}

// The hid tools' scripts, the pads' and the joystick's: the devices, a
// report pressing things, one letting everything go, a pause, and the
// end, which removes them; the joystick's ends two seconds earlier.
static bool WriteScripts(void)
{
    FILE* file = fopen(FILES "pads.json", "w");
    FILE* stick = fopen(FILES "joystick.json", "w");
    if (file == nullptr || stick == nullptr)
    {
        if (file != nullptr)
        {
            (void)fclose(file);
        }
        if (stick != nullptr)
        {
            (void)fclose(stick);
        }
        return false;
    }
    PutRegister(file, 0);
    PutRegister(file, 1);
    PutRegister(stick, 2);
    PutDelay(file, 1, 2, 2000);
    PutDelay(stick, 3, 3, 2000);
    // The Xbox pad: A and its fourth button (BTN_NORTH, Android's X), the
    // left stick right and up, the right stick left, the left trigger.
    const int16_t xbox[4] = {32767, -32768, -32768, 0};
    PutReport(file, 1, 0x0009, xbox, 255, 0, 8);
    // The generic pad: B, the hat up, the right stick right, the left
    // trigger (Z).
    const int16_t generic[4] = {0, 0, 32767, 0};
    PutReport(file, 2, 0x0002, generic, 255, 0, 0);
    fprintf(stick, "{\"id\":3,\"command\":\"report\",\"report\":[4,255,255]}\n");
    PutDelay(file, 1, 2, 700);
    PutDelay(stick, 3, 3, 700);
    const int16_t still[4] = {0, 0, 0, 0};
    PutReport(file, 1, 0, still, 0, 0, 8);
    PutReport(file, 2, 0, still, 0, 0, 8);
    fprintf(stick, "{\"id\":3,\"command\":\"report\",\"report\":[0,128,128]}\n");
    PutDelay(file, 1, 2, 3000);
    PutDelay(stick, 3, 3, 1000);
    bool closed = fclose(stick) == 0;
    return fclose(file) == 0 && closed;
}

static Pad* PadOf(Program* program, mwinGamepadId id)
{
    for (int i = 0; i < PADS; i++)
    {
        Pad* pad = &program->pads[i];
        if (pad->added && pad->id.index1 == id.index1 && pad->id.generation == id.generation)
        {
            return pad;
        }
    }
    return nullptr;
}

// A gamepad added: which of the three it is, by its name.
static void Added(Program* program, mwinContext* context, mwinGamepadId id)
{
    mwinGamepadInfo info = {0};
    if (mwinGetGamepadInfo(context, id, &info) != mwin_success)
    {
        return;
    }
    for (int i = 0; i < PADS; i++)
    {
        size_t length = strlen(s_names[i]);
        if (info.nameLength != length || memcmp(info.name, s_names[i], length) != 0)
        {
            continue;
        }
        Pad* pad = &program->pads[i];
        *pad = (Pad){.id = id, .added = true, .raw = !info.mapped};
        for (int axis = 0; axis < MWIN_GAMEPAD_RAW_AXES; axis++)
        {
            pad->lowest[axis] = 0.0f;
            pad->highest[axis] = 0.0f;
        }
        CHECK(info.vendor == s_vendors[i] && info.product == s_products[i],
              "a gamepad's vendor and product");
        CHECK(info.mapped == (i < 2), "the gamepads mapped, the joystick raw");
        CHECK(i < 2 || (info.rawButtons == 8 && info.rawAxes == 2),
              "the joystick's eight buttons and two axes");
        CHECK(info.capabilities == 0 && info.battery == -1, "no motors, sensors or battery");
        CHECK(mwinSetGamepadRumble(context, id, 0.5f, 0.5f, 100) == mwin_errorUnsupported &&
                  mwinSetGamepadMotion(context, id, true) == mwin_errorUnsupported,
              "no rumble without motors, no motion without sensors");
    }
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventGamepadAdded)
        {
            Added(program, context, event.data.gamepad);
            continue;
        }
        Pad* pad = nullptr;
        if (event.type == mwin_eventGamepadRemoved)
        {
            pad = PadOf(program, event.data.gamepad);
            if (pad != nullptr)
            {
                pad->removed = true;
                uint64_t* gone =
                    pad == &program->pads[2] ? &program->joystickGoneNs : &program->padGoneNs;
                *gone = *gone == 0 ? NowNs() : *gone;
            }
        }
        else if (event.type == mwin_eventGamepadButtonDown ||
                 event.type == mwin_eventGamepadButtonUp)
        {
            const mwinGamepadButtonEvent* button = &event.data.gamepadButton;
            pad = PadOf(program, button->gamepad);
            if (pad != nullptr)
            {
                uint32_t bit = 1u << (button->button & 31u);
                pad->downs |= event.type == mwin_eventGamepadButtonDown ? bit : 0;
                pad->ups |= event.type == mwin_eventGamepadButtonUp ? bit : 0;
                CHECK(button->raw == pad->raw, "a button raw as its gamepad");
            }
        }
        else if (event.type == mwin_eventGamepadAxisMoved)
        {
            const mwinGamepadAxisEvent* axis = &event.data.gamepadAxis;
            pad = PadOf(program, axis->gamepad);
            if (pad != nullptr && axis->axis < MWIN_GAMEPAD_RAW_AXES)
            {
                pad->lowest[axis->axis] = fminf(pad->lowest[axis->axis], axis->value);
                pad->highest[axis->axis] = fmaxf(pad->highest[axis->axis], axis->value);
            }
        }
    }
}

// Shows a frame, drawn on the CPU: Android 14 and later send nothing to
// a window whose surface never showed one.
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

static bool Released(const Pad* pad, uint32_t pressed)
{
    return pad->added && (pad->downs & pressed) == pressed && (pad->ups & pressed) == pressed;
}

static bool Ready(Program* program, mwinContext* context)
{
    const Pad* pads = program->pads;
    switch (program->phase)
    {
    case 0:
    {
        mwinWindowState state = {0};
        (void)mwinGetWindowState(context, program->window, &state);
        return state.focused;
    }
    case 1:
        return pads[0].added && pads[1].added && pads[2].added;
    case 2:
        if (program->releasedNs == 0 &&
            Released(&pads[0], 1u << mwin_padFaceSouth | 1u << mwin_padFaceWest) &&
            Released(&pads[1], 1u << mwin_padFaceEast | 1u << mwin_padDpadUp) &&
            Released(&pads[2], 1u << 2))
        {
            program->releasedNs = NowNs();
        }
        return program->releasedNs != 0 && NowNs() - program->releasedNs > 300000000u;
    case 3:
        return pads[0].removed && pads[1].removed && pads[2].removed;
    default:
        return true;
    }
}

static bool Near(float value, float expected)
{
    return fabsf(value - expected) < 0.02f;
}

static void CheckStill(Program* program, mwinContext* context)
{
    for (int i = 0; i < PADS; i++)
    {
        mwinGamepadState state = {0};
        CHECK(mwinGetGamepadState(context, program->pads[i].id, &state) == mwin_success &&
                  state.buttons == 0 && Near(state.axes[0], 0.0f) && Near(state.axes[1], 0.0f) &&
                  Near(state.axes[2], 0.0f) && Near(state.axes[4], 0.0f),
              "let go: nothing held, the sticks and triggers back");
    }
}

static void CheckMoves(const Program* program)
{
    const Pad* xbox = &program->pads[0];
    CHECK(Near(xbox->highest[mwin_padStickLeftX], 1.0f) &&
              Near(xbox->lowest[mwin_padStickLeftY], -1.0f),
          "the Xbox pad's left stick right and up");
    CHECK(Near(xbox->lowest[mwin_padStickRightX], -1.0f),
          "its right stick left, from RX (Android's Z)");
    CHECK(Near(xbox->highest[mwin_padTriggerLeft], 1.0f) &&
              xbox->highest[mwin_padTriggerRight] < 0.02f,
          "its left trigger only");
    const Pad* generic = &program->pads[1];
    CHECK(Near(generic->highest[mwin_padStickRightX], 1.0f),
          "the generic pad's right stick right, from RX");
    CHECK(Near(generic->highest[mwin_padTriggerLeft], 1.0f) &&
              generic->highest[mwin_padTriggerRight] < 0.02f,
          "its left trigger from Z, its right at rest from RZ");
    const Pad* joystick = &program->pads[2];
    CHECK(joystick->downs == 1u << 2 && Near(joystick->highest[0], 1.0f) &&
              Near(joystick->highest[1], 1.0f),
          "the joystick's third button and its axes, raw");
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
        Paint(program, context);
        CHECK(WriteScripts(), "the hid scripts written");
        printf("adb: sh -c 'run-as " MWIN_TEST_PACKAGE
               " cat files/pads.json | nohup hid - > /dev/null 2>&1 &'\n");
        printf("adb: sh -c 'run-as " MWIN_TEST_PACKAGE
               " cat files/joystick.json | nohup hid - > /dev/null 2>&1 &'\n");
        break;
    case 2:
        CheckStill(program, context);
        break;
    case 3:
        CheckMoves(program);
        // Its tool ends two seconds before theirs.
        CHECK(program->joystickGoneNs != 0 &&
                  program->padGoneNs > program->joystickGoneNs + 1000000000u,
              "the joystick removed a second or more before the pads");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
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
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d\n", program->phase);
        for (int i = 0; i < PADS; i++)
        {
            const Pad* pad = &program->pads[i];
            printf("pad %d: added %d removed %d downs %x ups %x hi %.2f %.2f %.2f %.2f %.2f %.2f "
                   "lo %.2f "
                   "%.2f %.2f %.2f\n",
                   i, pad->added, pad->removed, pad->downs, pad->ups, (double)pad->highest[0],
                   (double)pad->highest[1], (double)pad->highest[2], (double)pad->highest[3],
                   (double)pad->highest[4], (double)pad->highest[5], (double)pad->lowest[0],
                   (double)pad->lowest[1], (double)pad->lowest[2], (double)pad->lowest[3]);
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
