// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Which Linux input devices become gamepads, and how they are read, with
// virtual devices made through uinput: devices with gamepad buttons and
// a touch, pen or mouse button are not gamepads; a pad of the kernel's
// layout whose only hat axis is its vertical one gets the d-pad's up and
// down from it, and its analog triggers win over its trigger buttons; a
// database mapping that splits the left stick into a hat's halves; a pad
// whose force feedback refuses the effect fails rumble, and a stop before
// any rumble sends nothing; and with room for one gamepad, a second
// device is not added until the first leaves. Without a display or /dev/uinput the test is
// skipped (exit status 77).

#include "linux_uinput.h"

#include "maul-window/event.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEADLINE_NS 5000000000ull
// How long a device that is not a gamepad is given to be added anyway.
#define SETTLE_NS   300000000ull
#define MAX_RECORDS 64

typedef enum Phase
{
    phaseStart,
    phaseRefused,
    phaseHat,
    phaseHalves,
    phaseHalvesMoved,
    phaseFeedback,
    phaseStopped,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    uint64_t startNs;
    Device others[3];
    Device kernel;
    Device stick;
    Device feedback;
    mwinGamepadId pad;
    mwinEvent records[MAX_RECORDS];
    int count;
    int added;
    bool timedOut;
} Program;

// The kernel's gamepad layout, its triggers buttons too.
static const uint16_t s_kernelButtons[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,  BTN_TL,
                                           BTN_TR,    BTN_TL2,  BTN_TR2,   BTN_START, BTN_SELECT};
// Eleven buttons for a database mapping that numbers b0 to b10.
static const uint16_t s_stickButtons[] = {BTN_SOUTH, BTN_EAST, BTN_C,   BTN_NORTH, BTN_WEST,  BTN_Z,
                                          BTN_TL,    BTN_TR,   BTN_TL2, BTN_TR2,   BTN_SELECT};

typedef struct Spec
{
    const char* name;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
    const uint16_t* buttons;
    int buttonCount;
    // A button that makes the device something else than a gamepad, or 0.
    uint16_t other;
    bool triggers;
    bool hatX;
    bool feedback;
} Spec;

static bool Make(Device* device, const Spec* spec)
{
    int fd = open("/dev/uinput", O_RDWR);
    if (fd < 0)
    {
        perror("FAIL: opening /dev/uinput");
        return false;
    }
    (void)ioctl(fd, UI_SET_EVBIT, EV_KEY);
    (void)ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for (int i = 0; i < spec->buttonCount; i++)
    {
        (void)ioctl(fd, UI_SET_KEYBIT, spec->buttons[i]);
    }
    if (spec->other != 0)
    {
        (void)ioctl(fd, UI_SET_KEYBIT, spec->other);
    }
    Axis(fd, ABS_X, -32768, 32767);
    Axis(fd, ABS_Y, -32768, 32767);
    if (spec->triggers)
    {
        Axis(fd, ABS_Z, 0, 255);
        Axis(fd, ABS_RZ, 0, 255);
    }
    if (spec->hatX)
    {
        Axis(fd, ABS_HAT0X, -1, 1);
    }
    Axis(fd, ABS_HAT0Y, -1, 1);
    if (spec->feedback)
    {
        (void)ioctl(fd, UI_SET_EVBIT, EV_FF);
        (void)ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
    }
    struct uinput_setup setup = {.id = {BUS_USB, spec->vendor, spec->product, spec->version}};
    // Room for the effects of other programs on the machine that open
    // every pad.
    setup.ff_effects_max = spec->feedback ? 16 : 0;
    strncpy(setup.name, spec->name, UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
    {
        perror("FAIL: making a uinput device");
        close(fd);
        return false;
    }
    *device = (Device){.fd = fd, .feedback = spec->feedback};
    pthread_mutex_init(&device->lock, nullptr);
    if (spec->feedback)
    {
        (void)pthread_create(&device->thread, nullptr, Feedback, device);
    }
    return true;
}

// A pad of the kernel's layout the database lacks: no d-pad buttons, a
// hat with only its vertical axis, analog triggers.
static const Spec s_kernel = {
    "Kernel Hat Pad", 0x1234, 0x0003, 1, s_kernelButtons, 10, 0, true, false, false};

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventGamepadAdded)
        {
            program->added += 1;
            program->pad = event.data.gamepad;
        }
        if (program->count < MAX_RECORDS)
        {
            program->records[program->count++] = event;
        }
    }
}

static bool Pressed(const Program* program, uint8_t button)
{
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        if (event->type == mwin_eventGamepadButtonDown &&
            event->data.gamepadButton.button == button)
        {
            return true;
        }
    }
    return false;
}

static bool Settled(const Program* program)
{
    return NowNs() - program->startNs > SETTLE_NS;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseStart:
        return true;
    case phaseRefused:
    case phaseHalves:
    case phaseFeedback:
        return program->added > 0 && Settled(program);
    case phaseHat:
        return Pressed(program, mwin_padDpadUp);
    case phaseHalvesMoved:
        return Pressed(program, mwin_padFaceSouth) || Settled(program);
    default:
        return Settled(program);
    }
}

static void CheckPad(mwinContext* context, mwinGamepadId pad, uint16_t vendor, uint16_t product,
                     const char* what)
{
    mwinGamepadInfo info;
    CHECK(mwinGetGamepadInfo(context, pad, &info) == mwin_success && info.mapped &&
              info.vendor == vendor && info.product == product,
          what);
}

static void Advance(Program* program, mwinContext* context)
{
    static const uint16_t others[3] = {BTN_TOUCH, BTN_TOOL_PEN, BTN_LEFT};
    mwinGamepadState state;
    switch (program->phase)
    {
    case phaseStart:
        // Gamepad buttons with a touch, a pen or a mouse button, then a
        // gamepad: only it comes.
        for (int i = 0; i < 3; i++)
        {
            Spec spec = s_kernel;
            spec.name = "Not A Pad";
            spec.product = (uint16_t)(0x0010 + i);
            spec.other = others[i];
            CHECK(Make(&program->others[i], &spec), "a device that is not a gamepad");
        }
        CHECK(Make(&program->kernel, &s_kernel), "a gamepad after them");
        break;
    case phaseRefused:
        CHECK(program->added == 1, "only the gamepad is added");
        CheckPad(context, program->pad, 0x1234, 0x0003, "the gamepad, of the kernel's layout");
        Emit(&program->kernel, EV_ABS, ABS_HAT0Y, -1);
        Emit(&program->kernel, EV_ABS, ABS_Z, 255);
        Emit(&program->kernel, EV_KEY, BTN_TL2, 1);
        Emit(&program->kernel, EV_KEY, BTN_TR2, 1);
        Report(&program->kernel);
        break;
    case phaseHat:
        CHECK(mwinGetGamepadState(context, program->pad, &state) == mwin_success &&
                  (state.buttons & (1u << mwin_padDpadUp)) != 0,
              "up on the d-pad, from the hat's only axis");
        CHECK(state.axes[mwin_padTriggerLeft] == 1.0f && state.axes[mwin_padTriggerRight] == 0.0f,
              "the triggers from their axes, not their buttons");
        program->added = 0;
        CHECK(Make(&program->stick, &(Spec){"Hori Fightstick VX", 0x1BAD, 0xF503, 0x0533,
                                            s_stickButtons, 11, 0, false, true, false}),
              "a pad the database maps with a stick from a hat's halves");
        break;
    case phaseHalves:
        CheckPad(context, program->pad, 0x1BAD, 0xF503, "the mapped pad");
        Emit(&program->stick, EV_ABS, ABS_HAT0X, 1);
        Emit(&program->stick, EV_ABS, ABS_HAT0Y, -1);
        Emit(&program->stick, EV_KEY, BTN_SOUTH, 1);
        Report(&program->stick);
        break;
    case phaseHalvesMoved:
        CHECK(Pressed(program, mwin_padFaceSouth), "its first button");
        CHECK(mwinGetGamepadState(context, program->pad, &state) == mwin_success &&
                  state.axes[mwin_padStickLeftX] == 1.0f && state.axes[mwin_padStickLeftY] == -1.0f,
              "the hat right and up: the left stick right and up");
        program->added = 0;
        {
            Spec spec = s_kernel;
            spec.name = "Refusing Pad";
            spec.product = 0x0005;
            spec.feedback = true;
            CHECK(Make(&program->feedback, &spec), "a pad whose force feedback refuses effects");
            pthread_mutex_lock(&program->feedback.lock);
            program->feedback.uploadResult = -EIO;
            pthread_mutex_unlock(&program->feedback.lock);
        }
        break;
    case phaseFeedback:
    {
        mwinGamepadInfo info;
        CHECK(mwinGetGamepadInfo(context, program->pad, &info) == mwin_success &&
                  (info.capabilities & mwin_padRumble) != 0,
              "it has motors");
        CHECK(mwinSetGamepadRumble(context, program->pad, 0.0f, 0.0f, 0) == mwin_success,
              "a stop before any rumble");
        CHECK(mwinSetGamepadRumble(context, program->pad, 1.0f, 1.0f, 100) == mwin_errorPlatform,
              "rumble the device refuses");
        break;
    }
    default:
        pthread_mutex_lock(&program->feedback.lock);
        // Other programs on the machine that open every pad may upload
        // effects of their own, never played by this test.
        CHECK(program->feedback.uploads >= 1 && program->feedback.plays == 0 &&
                  program->feedback.stops == 0,
              "the stop sent nothing, the refused effect was not played");
        pthread_mutex_unlock(&program->feedback.lock);
        break;
    }
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (Ready(program))
    {
        Advance(program, context);
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
        // hygiene: yields between frames; the wait ends on a report, within the deadline
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    Program* program = user;
    program->startNs = NowNs();
    return mwin_success;
}

// With room for one gamepad, a second device is not added, until the
// first leaves.
typedef struct Full
{
    int phase;
    uint64_t startNs;
    Device* first;
    Device second;
    int added;
    int removed;
    mwinGamepadId pad;
    bool timedOut;
} Full;

static mwinFrameResult FullFrame(mwinContext* context, void* user)
{
    Full* full = user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        full->added += event.type == mwin_eventGamepadAdded ? 1 : 0;
        full->removed += event.type == mwin_eventGamepadRemoved ? 1 : 0;
        full->pad = event.type == mwin_eventGamepadAdded ? event.data.gamepad : full->pad;
    }
    if (full->phase == 0 && full->added == 1)
    {
        Spec spec = s_kernel;
        spec.name = "Second Pad";
        spec.product = 0x0006;
        CHECK(Make(&full->second, &spec), "a second gamepad");
        full->phase = 1;
        full->startNs = NowNs();
    }
    else if (full->phase == 1 && NowNs() - full->startNs > SETTLE_NS)
    {
        CHECK(full->added == 1, "no room: the second is not added");
        Destroy(full->first);
        full->phase = 2;
        full->startNs = NowNs();
    }
    else if (full->phase == 2 && full->added == 2)
    {
        mwinGamepadInfo info;
        CHECK(full->removed == 1 && mwinGetGamepadInfo(context, full->pad, &info) == mwin_success &&
                  info.product == 0x0006,
              "the first gone, the second is added");
        return mwin_frameStop;
    }
    else if (NowNs() - full->startNs > DEADLINE_NS)
    {
        full->timedOut = true;
        return mwin_frameStop;
    }
    struct timespec pause = {0, 1000000};
    // hygiene: yields between frames; the wait ends on a report, within the deadline
    (void)nanosleep(&pause, nullptr);
    return mwin_frameContinue;
}

static mwinResult FullInit(mwinContext* context, void* user)
{
    (void)context;
    ((Full*)user)->startNs = NowNs();
    return mwin_success;
}

static void TestFull(void)
{
    static Full full;
    full.second.fd = -1;
    Device first = {.fd = -1};
    Spec spec = s_kernel;
    spec.name = "First Pad";
    if (!Make(&first, &spec))
    {
        s_failures += 1;
        return;
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.gamepads = 1;
    def.init = FullInit;
    def.frame = FullFrame;
    def.user = &full;
    full.first = &first;
    CHECK(mwinRun(&def) == mwin_success && !full.timedOut, "the program with room for one runs");
    Destroy(&first);
    Destroy(&full.second);
}

static void TestDevices(void)
{
    static Program program;
    for (int i = 0; i < 3; i++)
    {
        program.others[i].fd = -1;
    }
    program.kernel.fd = -1;
    program.stick.fd = -1;
    program.feedback.fd = -1;
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
    for (int i = 0; i < 3; i++)
    {
        Destroy(&program.others[i]);
    }
    Destroy(&program.kernel);
    Destroy(&program.stick);
    Destroy(&program.feedback);
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    const char* wayland = getenv("WAYLAND_DISPLAY");
    if ((display == nullptr || display[0] == '\0') && (wayland == nullptr || wayland[0] == '\0'))
    {
        // hygiene: no display here is a skip, and CI requires one
        return 77;
    }
    // Without /dev/uinput open for writing there are no pads to make.
    int uinput = open("/dev/uinput", O_RDWR);
    if (uinput < 0)
    {
        // hygiene: no writable /dev/uinput is a skip, and CI requires one
        return 77;
    }
    close(uinput);
    TestDevices();
    TestFull();
    return s_failures == 0 ? 0 : 1;
}
