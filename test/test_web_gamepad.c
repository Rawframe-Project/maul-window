// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's gamepads in headless Chrome (test/web_runner.cjs),
// against a stand-in for navigator.getGamepads() the test plays between
// frames: a standard pad and a raw one found with their names, vendors
// and products from Chrome's and Firefox's ids; their controls posted
// when their timestamps move and not before, the standard mapping's
// buttons, sticks and analog triggers placed, raw values kept in range;
// dual-rumble played and reset; a pad swapped for another at its index
// between two frames, and one gone.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/event.h"
#include "maul-window/gamepad.h"

#include <string.h>

#define MAX_RECORDS 64

typedef enum Phase
{
    phaseFound,
    phaseMoved,
    phaseStill,
    phaseSwapped,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    mwinGamepadId standard;
    mwinGamepadId raw;
    mwinEvent records[MAX_RECORDS];
    int count;
} Program;

// clang-format off
// A standard pad at index 0 and a raw one at 1; neither pressed.
EM_JS(void, Install, (void), {
    const pad = (index, id, mapping, buttons, axes, actuator) => ({
        index, id, mapping, connected: true, timestamp: 1,
        buttons: Array.from({length: buttons}, () => ({pressed: false, value: 0})),
        axes: new Array(axes).fill(0), vibrationActuator: actuator});
    const effects = [];
    const actuator = {effects: ['dual-rumble'],
        playEffect: (type, params) => { effects.push([type, params]); return Promise.resolve('complete'); },
        reset: () => { effects.push(['reset']); return Promise.resolve('complete'); }};
    const pads = [
        pad(0, 'Xbox 360 Controller (STANDARD GAMEPAD Vendor: 045e Product: 028e)', 'standard', 17,
            4, actuator),
        pad(1, '54c-9cc-Wireless Controller', "", 3, 2, null)];
    globalThis.mwinPads = {pad, pads, effects};
    Object.defineProperty(navigator, 'getGamepads', {value: () => pads, configurable: true});
});

// Presses and moves both pads, with new timestamps.
EM_JS(void, Move, (void), {
    const [standard, raw] = globalThis.mwinPads.pads;
    [0, 12, 16].forEach(i => standard.buttons[i].pressed = true);
    standard.buttons[7].value = 0.5;
    standard.axes[1] = -1;
    standard.timestamp = 2;
    raw.buttons[2].pressed = true;
    raw.axes[0] = 2;
    raw.axes[1] = 0.25;
    raw.timestamp = 2;
});

// Lets go of the standard pad's button, its timestamp as it was.
EM_JS(void, Unstamped, (void), {
    globalThis.mwinPads.pads[0].buttons[0].pressed = false;
});

// The effects played, as "type strong weak duration" joined by ";".
EM_JS(bool, Played, (const char* expected), {
    const text = globalThis.mwinPads.effects.map(([type, params]) =>
        params ? `${type} ${params.strongMagnitude} ${params.weakMagnitude} ${params.duration}`
               : type).join(';');
    return text === UTF8ToString(expected);
});

// The standard pad disconnected and another at its index; the raw one
// gone.
EM_JS(void, Swap, (void), {
    const state = globalThis.mwinPads;
    const old = state.pads[0];
    state.pads[0] = state.pad(0, 'Plain Pad', 'standard', 17, 4, null);
    state.pads[1] = null;
    window.dispatchEvent(Object.assign(new Event('gamepaddisconnected'), {gamepad: old}));
});
// clang-format on

EM_JS_DEPS(test_web_gamepad, "$UTF8ToString");

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

static bool Same(mwinGamepadId a, mwinGamepadId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static bool Button(const Program* program, mwinGamepadId pad, uint8_t button, bool raw)
{
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        if (event->type == mwin_eventGamepadButtonDown &&
            Same(event->data.gamepadButton.gamepad, pad) &&
            event->data.gamepadButton.button == button && event->data.gamepadButton.raw == raw)
        {
            return true;
        }
    }
    return false;
}

static bool Axis(const Program* program, mwinGamepadId pad, uint8_t axis, float value)
{
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        if (event->type == mwin_eventGamepadAxisMoved &&
            Same(event->data.gamepadAxis.gamepad, pad) && event->data.gamepadAxis.axis == axis &&
            event->data.gamepadAxis.value == value)
        {
            return true;
        }
    }
    return false;
}

static bool Named(mwinContext* context, mwinGamepadId pad, const char* name, mwinGamepadInfo* info)
{
    return mwinGetGamepadInfo(context, pad, info) == mwin_success &&
           info->nameLength == strlen(name) && memcmp(info->name, name, info->nameLength) == 0;
}

static void CheckFound(Program* program, mwinContext* context)
{
    const mwinEvent* first = Find(program, mwin_eventGamepadAdded, 0);
    const mwinEvent* second = Find(program, mwin_eventGamepadAdded, 1);
    CHECK(first != nullptr && second != nullptr, "both pads found");
    if (first == nullptr || second == nullptr)
    {
        return;
    }
    program->standard = first->data.gamepad;
    program->raw = second->data.gamepad;
    mwinGamepadInfo info;
    CHECK(Named(context, program->standard, "Xbox 360 Controller", &info) && info.mapped &&
              info.vendor == 0x045e && info.product == 0x028e &&
              info.capabilities == mwin_padRumble && info.battery == -1,
          "the standard pad: Chrome's id, mapped, with rumble");
    CHECK(Named(context, program->raw, "Wireless Controller", &info) && !info.mapped &&
              info.rawButtons == 3 && info.rawAxes == 2 && info.vendor == 0x054c &&
              info.product == 0x09cc && info.capabilities == 0,
          "the raw pad: Firefox's id, its buttons and axes");
    Move();
}

static void CheckMoved(Program* program, mwinContext* context)
{
    mwinGamepadId pad = program->standard;
    CHECK(Button(program, pad, mwin_padFaceSouth, false) &&
              Button(program, pad, mwin_padDpadUp, false) &&
              Button(program, pad, mwin_padGuide, false),
          "the standard buttons placed");
    CHECK(Axis(program, pad, mwin_padStickLeftY, -1.0f) &&
              Axis(program, pad, mwin_padTriggerRight, 0.5f),
          "a stick up, a trigger half in");
    CHECK(Button(program, program->raw, 2, true) && Axis(program, program->raw, 0, 1.0f) &&
              Axis(program, program->raw, 1, 0.25f),
          "raw controls by number, in range");
    CHECK(mwinSetGamepadRumble(context, pad, 1.0f, 0.5f, 200) == mwin_success &&
              mwinSetGamepadRumble(context, pad, 0.0f, 0.0f, 0) == mwin_success &&
              Played("dual-rumble 1 0.5 200;reset"),
          "dual-rumble played, then reset");
    CHECK(mwinSetGamepadRumble(context, program->raw, 1.0f, 1.0f, 100) == mwin_errorUnsupported,
          "no rumble without an actuator");
    Unstamped();
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseFound:
        CheckFound(program, context);
        break;
    case phaseMoved:
        CheckMoved(program, context);
        break;
    case phaseStill:
        CHECK(program->count == 0, "an unchanged timestamp posts nothing");
        Swap();
        break;
    default:
    {
        const mwinEvent* removed = Find(program, mwin_eventGamepadRemoved, 0);
        const mwinEvent* gone = Find(program, mwin_eventGamepadRemoved, 1);
        const mwinEvent* added = Find(program, mwin_eventGamepadAdded, 0);
        mwinGamepadInfo info;
        CHECK(removed != nullptr && gone != nullptr && added != nullptr &&
                  Same(removed->data.gamepad, program->standard) &&
                  Same(gone->data.gamepad, program->raw) && removed < added &&
                  Named(context, added->data.gamepad, "Plain Pad", &info) && info.vendor == 0,
              "the swapped pad removed and the new one added, the gone one removed");
        break;
    }
    }
    program->phase += 1;
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    Install();
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    program->count = 0;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
    Advance(program, context);
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    const Program* program = user;
    CHECK(status == mwin_success && program->phase == phaseDone, "every phase ran");
    (void)printf("mwin-test: exit %d\n", s_failures == 0 ? 0 : 1);
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    // With Emscripten mwinRun returns only when init failed; without it,
    // it returns once init succeeded and the page's frames run the program
    // on (mwin-0022). Either way quit reports.
    if (mwinRun(&def) == mwin_success)
    {
        return 0;
    }
    (void)printf("mwin-test: exit 1\n");
    return 1;
}
