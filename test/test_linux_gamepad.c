// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Linux gamepads through the Wayland or X11 backend, with virtual
// devices made through uinput: an Xbox 360 pad connected before the
// program starts, mapped by SDL_GameControllerDB, its buttons, sticks,
// triggers and d-pad hat, and its rumble; a pad of the kernel's layout
// the database lacks, connected while the program runs, then its motion
// sensors device, which grants it motion: samples in units per g and
// per degree per second, timed by their stamps; a joystick of
// no known layout, raw, its hat as two axes; and a disconnect. Without
// a display or /dev/uinput the test is skipped (exit status 77).

#include "test_harness.h"

#include "maul-window/event.h"

#include <fcntl.h>
#include <linux/uinput.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 128

typedef enum Phase
{
    phaseStart,
    phaseXbox,
    phaseKernel,
    phaseKernelPress,
    phaseMotionFound,
    phaseMotion,
    phaseRaw,
    phaseRawPress,
    phaseRumble,
    phaseRemove,
    phaseDone,
} Phase;

// A virtual device, and what its force feedback was asked.
typedef struct Device
{
    int fd;
    pthread_t thread;
    bool feedback;
    pthread_mutex_t lock;
    uint16_t strong;
    uint16_t weak;
    uint16_t length;
    int plays;
} Device;

typedef struct Program
{
    Phase phase;
    uint64_t startNs;
    Device xbox;
    Device kernel;
    Device motion;
    Device raw;
    mwinGamepadId pads[3];
    mwinEvent records[MAX_RECORDS];
    int count;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Emit(const Device* device, uint16_t type, uint16_t code, int32_t value)
{
    struct input_event event = {.type = type, .code = code, .value = value};
    CHECK(write(device->fd, &event, sizeof(event)) == (ssize_t)sizeof(event), "an event");
}

static void Report(const Device* device)
{
    Emit(device, EV_SYN, SYN_REPORT, 0);
}

static void AxisWith(int fd, uint16_t code, int32_t minimum, int32_t maximum, int32_t resolution)
{
    struct uinput_abs_setup setup = {.code = code};
    setup.absinfo.minimum = minimum;
    setup.absinfo.maximum = maximum;
    setup.absinfo.resolution = resolution;
    (void)ioctl(fd, UI_SET_ABSBIT, code);
    (void)ioctl(fd, UI_ABS_SETUP, &setup);
}

static void Axis(int fd, uint16_t code, int32_t minimum, int32_t maximum)
{
    AxisWith(fd, code, minimum, maximum, 0);
}

// The physical path the kernel pad and its motion device share, as a
// controller's parts do.
#define KERNEL_PHYS "maul-test/kernel-pad"

// Answers the force feedback uploads the driver makes, which block until
// the device answers.
static void* Feedback(void* data)
{
    Device* device = data;
    struct input_event event;
    while (read(device->fd, &event, sizeof(event)) == (ssize_t)sizeof(event))
    {
        if (event.type == EV_UINPUT && event.code == UI_FF_UPLOAD)
        {
            struct uinput_ff_upload upload = {.request_id = (uint32_t)event.value};
            (void)ioctl(device->fd, UI_BEGIN_FF_UPLOAD, &upload);
            pthread_mutex_lock(&device->lock);
            device->strong = upload.effect.u.rumble.strong_magnitude;
            device->weak = upload.effect.u.rumble.weak_magnitude;
            device->length = upload.effect.replay.length;
            pthread_mutex_unlock(&device->lock);
            upload.retval = 0;
            (void)ioctl(device->fd, UI_END_FF_UPLOAD, &upload);
        }
        else if (event.type == EV_UINPUT && event.code == UI_FF_ERASE)
        {
            struct uinput_ff_erase erase = {.request_id = (uint32_t)event.value};
            (void)ioctl(device->fd, UI_BEGIN_FF_ERASE, &erase);
            (void)ioctl(device->fd, UI_END_FF_ERASE, &erase);
        }
        else if (event.type == EV_FF && event.value > 0)
        {
            pthread_mutex_lock(&device->lock);
            device->plays += 1;
            pthread_mutex_unlock(&device->lock);
        }
    }
    return nullptr;
}

// Makes a virtual device with buttons, axes, and force feedback or not.
static bool Make(Device* device, const char* name, uint16_t vendor, uint16_t product,
                 uint16_t version, const uint16_t* buttons, int buttonCount, bool xbox)
{
    int fd = open("/dev/uinput", O_RDWR);
    if (fd < 0)
    {
        return false;
    }
    (void)ioctl(fd, UI_SET_EVBIT, EV_KEY);
    (void)ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for (int i = 0; i < buttonCount; i++)
    {
        (void)ioctl(fd, UI_SET_KEYBIT, buttons[i]);
    }
    Axis(fd, ABS_X, -32768, 32767);
    Axis(fd, ABS_Y, -32768, 32767);
    if (xbox)
    {
        Axis(fd, ABS_Z, 0, 255);
        Axis(fd, ABS_RX, -32768, 32767);
        Axis(fd, ABS_RY, -32768, 32767);
        Axis(fd, ABS_RZ, 0, 255);
        (void)ioctl(fd, UI_SET_EVBIT, EV_FF);
        (void)ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
    }
    Axis(fd, ABS_HAT0X, -1, 1);
    Axis(fd, ABS_HAT0Y, -1, 1);
    if (vendor == 0x1234 && product == 0x0001)
    {
        (void)ioctl(fd, UI_SET_PHYS, KERNEL_PHYS);
    }
    struct uinput_setup setup = {.id = {BUS_USB, vendor, product, version}};
    setup.ff_effects_max = xbox ? 1 : 0;
    strncpy(setup.name, name, UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
    {
        close(fd);
        return false;
    }
    *device = (Device){.fd = fd, .feedback = xbox};
    pthread_mutex_init(&device->lock, nullptr);
    if (xbox)
    {
        (void)pthread_create(&device->thread, nullptr, Feedback, device);
    }
    return true;
}

// Makes the kernel pad's motion sensors device, as hid-playstation's: 8192
// units per g, 1024 per degree per second, stamped in microseconds.
static bool MakeMotion(Device* device)
{
    int fd = open("/dev/uinput", O_RDWR);
    if (fd < 0)
    {
        return false;
    }
    (void)ioctl(fd, UI_SET_EVBIT, EV_ABS);
    (void)ioctl(fd, UI_SET_EVBIT, EV_MSC);
    (void)ioctl(fd, UI_SET_MSCBIT, MSC_TIMESTAMP);
    (void)ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_ACCELEROMETER);
    for (uint16_t code = ABS_X; code <= ABS_Z; code++)
    {
        AxisWith(fd, code, -32768, 32767, 8192);
    }
    for (uint16_t code = ABS_RX; code <= ABS_RZ; code++)
    {
        AxisWith(fd, code, -2048000, 2048000, 1024);
    }
    (void)ioctl(fd, UI_SET_PHYS, KERNEL_PHYS);
    struct uinput_setup setup = {.id = {BUS_USB, 0x1234, 0x0001, 1}};
    strncpy(setup.name, "Kernel Pad Motion Sensors", UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
    {
        close(fd);
        return false;
    }
    *device = (Device){.fd = fd};
    return true;
}

// A motion sample: still, gravity along y, turning at 90 degrees per
// second about x.
static void Sample(const Device* device, int32_t stamp)
{
    Emit(device, EV_ABS, ABS_Y, 8192);
    Emit(device, EV_ABS, ABS_RX, 90 * 1024);
    Emit(device, EV_MSC, MSC_TIMESTAMP, stamp);
    Report(device);
}

static void Destroy(Device* device)
{
    if (device->fd < 0)
    {
        return;
    }
    (void)ioctl(device->fd, UI_DEV_DESTROY);
    if (device->feedback)
    {
        (void)pthread_cancel(device->thread);
        (void)pthread_join(device->thread, nullptr);
    }
    close(device->fd);
    device->fd = -1;
}

static const uint16_t s_xboxButtons[] = {BTN_A,    BTN_B,      BTN_X,      BTN_Y,
                                         BTN_TL,   BTN_TR,     BTN_SELECT, BTN_START,
                                         BTN_MODE, BTN_THUMBL, BTN_THUMBR};
static const uint16_t s_kernelButtons[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
                                           BTN_TL,    BTN_TR,   BTN_START, BTN_SELECT};
static const uint16_t s_rawButtons[] = {BTN_TRIGGER, BTN_THUMB};

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
}

static const mwinEvent* Find(const Program* program, mwinEventType type, int nth)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type && nth-- == 0)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

// The last value an axis record gave, or NAN.
static float AxisValue(const Program* program, uint8_t axis, bool raw)
{
    float value = NAN;
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        if (event->type == mwin_eventGamepadAxisMoved && event->data.gamepadAxis.axis == axis &&
            event->data.gamepadAxis.raw == raw)
        {
            value = event->data.gamepadAxis.value;
        }
    }
    return value;
}

static bool Pressed(const Program* program, uint8_t button, bool raw)
{
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        if (event->type == mwin_eventGamepadButtonDown &&
            event->data.gamepadButton.button == button && event->data.gamepadButton.raw == raw)
        {
            return true;
        }
    }
    return false;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseStart:
    case phaseKernel:
    case phaseRaw:
        return Find(program, mwin_eventGamepadAdded, 0) != nullptr;
    case phaseXbox:
        return Pressed(program, mwin_padDpadUp, false);
    case phaseKernelPress:
        return Pressed(program, mwin_padFaceNorth, false);
    case phaseMotionFound:
        return Find(program, mwin_eventGamepadChanged, 0) != nullptr;
    case phaseMotion:
        // Frames have read the samples.
        return NowNs() - program->startNs > 50000000u;
    case phaseRawPress:
        return Find(program, mwin_eventGamepadButtonDown, 0) != nullptr &&
               AxisValue(program, 2, true) == 1.0f;
    case phaseRumble:
        return true;
    default:
        return Find(program, mwin_eventGamepadRemoved, 0) != nullptr;
    }
}

// Checks a new gamepad's facts, and keeps its id.
static void CheckAdded(Program* program, mwinContext* context, int index, bool mapped)
{
    mwinGamepadInfo info;
    program->pads[index] = Find(program, mwin_eventGamepadAdded, 0)->data.gamepad;
    CHECK(mwinGetGamepadInfo(context, program->pads[index], &info) == mwin_success &&
              info.mapped == mapped && info.vendor == (index == 0 ? 0x045E : 0x1234),
          "a gamepad and its facts");
    if (index == 0)
    {
        CHECK(info.nameLength == 7 && memcmp(info.name, "Xbox Pi", 7) == 0 &&
                  (info.capabilities & mwin_padRumble) != 0,
              "its name, and its motors");
    }
    if (index == 2)
    {
        CHECK(info.rawButtons == 2 && info.rawAxes == 4, "raw: two buttons, two axes and a hat");
    }
}

static void AdvanceXbox(Program* program, mwinContext* context)
{
    if (program->phase == phaseStart)
    {
        CheckAdded(program, context, 0, true);
        Emit(&program->xbox, EV_KEY, BTN_A, 1);
        Emit(&program->xbox, EV_KEY, BTN_X, 1);
        Emit(&program->xbox, EV_ABS, ABS_X, 32767);
        Emit(&program->xbox, EV_ABS, ABS_Z, 255);
        Emit(&program->xbox, EV_ABS, ABS_RY, -32768);
        Emit(&program->xbox, EV_ABS, ABS_HAT0Y, -1);
        Report(&program->xbox);
        return;
    }
    CHECK(Pressed(program, mwin_padFaceSouth, false) && Pressed(program, mwin_padFaceWest, false),
          "A south, X west, as the database maps them");
    CHECK(AxisValue(program, mwin_padStickLeftX, false) == 1.0f &&
              AxisValue(program, mwin_padTriggerLeft, false) == 1.0f &&
              AxisValue(program, mwin_padStickRightY, false) == -1.0f,
          "a stick right, a trigger in, a stick up");
    mwinGamepadState state;
    CHECK(mwinGetGamepadState(context, program->pads[0], &state) == mwin_success &&
              (state.buttons & (1u << mwin_padDpadUp)) != 0,
          "the d-pad from the hat, in the state");
    CHECK(Make(&program->kernel, "Kernel Pad", 0x1234, 0x0001, 1, s_kernelButtons, 8, false),
          "a pad connected while the program runs");
}

static void AdvanceOthers(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseKernel:
        CheckAdded(program, context, 1, true);
        Emit(&program->kernel, EV_KEY, BTN_NORTH, 1);
        Report(&program->kernel);
        break;
    case phaseKernelPress:
    {
        mwinGamepadInfo info;
        CHECK(mwinGetGamepadInfo(context, program->pads[1], &info) == mwin_success &&
                  (info.capabilities & mwin_padMotion) == 0 &&
                  mwinSetGamepadMotion(context, program->pads[1], true) == mwin_errorUnsupported,
              "no motion before its sensors come");
        CHECK(MakeMotion(&program->motion), "its motion sensors device");
        break;
    }
    case phaseMotionFound:
    {
        mwinGamepadInfo info;
        CHECK(mwinGetGamepadInfo(context, program->pads[1], &info) == mwin_success &&
                  (info.capabilities & mwin_padMotion) != 0,
              "the sensors device of the same path grants motion");
        CHECK(mwinSetGamepadMotion(context, program->pads[1], true) == mwin_success,
              "motion turned on");
        // Three samples 4 ms apart, the stamps wrapping between them.
        Sample(&program->motion, -6000);
        Sample(&program->motion, -2000);
        Sample(&program->motion, 2000);
        break;
    }
    case phaseMotion:
    {
        mwinGamepadMotion motion;
        CHECK(mwinGetGamepadMotion(context, program->pads[1], &motion) == mwin_success &&
                  fabsf(motion.acceleration[1] - 9.80665f) < 1e-4f &&
                  motion.acceleration[0] == 0.0f &&
                  fabsf(motion.rotationRate[0] - 1.5707964f) < 1e-4f &&
                  fabsf(motion.rotation[0] - 1.5707964f * 0.008f) < 1e-5f,
              "gravity in m/s^2, the rate in rad/s, the angle over the stamps' 8 ms");
        Destroy(&program->motion);
        CHECK(Make(&program->raw, "Stick", 0x1234, 0x0002, 1, s_rawButtons, 2, false),
              "a joystick");
        break;
    }
    case phaseRaw:
        CheckAdded(program, context, 2, false);
        Emit(&program->raw, EV_KEY, BTN_THUMB, 1);
        Emit(&program->raw, EV_ABS, ABS_HAT0X, 1);
        Report(&program->raw);
        break;
    case phaseRawPress:
    {
        const mwinEvent* down = Find(program, mwin_eventGamepadButtonDown, 0);
        CHECK(down->data.gamepadButton.raw && down->data.gamepadButton.button == 1,
              "raw: the second button");
        CHECK(mwinSetGamepadRumble(context, program->pads[0], 1.0f, 0.5f, 250) == mwin_success,
              "rumble");
        break;
    }
    case phaseRumble:
        pthread_mutex_lock(&program->xbox.lock);
        CHECK(program->xbox.strong == 65535 && program->xbox.weak == 32767 &&
                  program->xbox.length == 250,
              "force feedback with both motors' strengths");
        pthread_mutex_unlock(&program->xbox.lock);
        CHECK(mwinSetGamepadRumble(context, program->pads[1], 1.0f, 1.0f, 100) ==
                  mwin_errorUnsupported,
              "no rumble without motors");
        Destroy(&program->kernel);
        break;
    default:
        CHECK(Find(program, mwin_eventGamepadRemoved, 0)->data.gamepad.index1 ==
                  program->pads[1].index1,
              "a disconnect");
        break;
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
    Collect(program, context);
    if (Ready(program))
    {
        if (program->phase <= phaseXbox)
        {
            AdvanceXbox(program, context);
        }
        else
        {
            AdvanceOthers(program, context);
        }
        program->phase += 1;
        program->count = 0;
        program->startNs = NowNs();
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 1000000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    const char* wayland = getenv("WAYLAND_DISPLAY");
    if ((display == nullptr || display[0] == '\0') && (wayland == nullptr || wayland[0] == '\0'))
    {
        return 77;
    }
    static Program program;
    program.kernel.fd = -1;
    program.motion.fd = -1;
    program.raw.fd = -1;
    // Connected before the program starts: found when it does.
    if (!Make(&program.xbox, "Xbox Pi", 0x045E, 0x028E, 0x0114, s_xboxButtons, 11, true))
    {
        return 77;
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    CHECK(!program.timedOut, "every phase completes in time");
    if (program.timedOut)
    {
        (void)printf("timed out in phase %d\n", (int)program.phase);
    }
    Destroy(&program.xbox);
    Destroy(&program.kernel);
    Destroy(&program.motion);
    Destroy(&program.raw);
    return s_failures == 0 ? 0 : 1;
}
