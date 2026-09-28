// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes numbered gamepad controls through their mapping, as the
// backends read them from devices: a mapping (or none, for a raw pad)
// and the halves of its sticks, then reports of buttons, axes from -1
// to 1 and hats. Each input runs on a context of the test backend. A
// mapped pad's sticks must stay within -1 and 1 and its triggers within
// 0 and 1; a raw pad's axes within -1 and 1; no button past the pad's.

#include "core.h"
#include "pad_map.h"

#include "maul-window/gamepad.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t* s_data;
static size_t s_size;

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static void Take(void* out, size_t bytes)
{
    size_t taken = bytes < s_size ? bytes : s_size;
    memset(out, 0, bytes);
    memcpy(out, s_data, taken);
    s_data += taken;
    s_size -= taken;
}

static void Check(const mwinContext* context, uint32_t slot, const mwinPadControls* controls)
{
    const mwinGamepadState* state = &context->gamepads[slot].state;
    uint32_t buttons = controls->mapping != nullptr ? MWIN_GAMEPAD_BUTTONS
                                                    : context->gamepads[slot].info.rawButtons;
    Expect(buttons == 32 || (state->buttons >> buttons) == 0);
    for (int i = 0; i < MWIN_GAMEPAD_RAW_AXES; i++)
    {
        float value = state->axes[i];
        bool trigger = controls->mapping != nullptr && i >= mwin_padTriggerLeft;
        Expect(isfinite(value) && value >= (trigger ? 0.0f : -1.0f) && value <= 1.0f);
    }
}

// Reads a report's controls: buttons as bits, axes from -1 to 1, hats'
// direction bits.
static void TakeReport(mwinPadControls* controls)
{
    uint64_t down = 0;
    int8_t axes[MWIN_PAD_NUMBERED_AXES];
    uint8_t hats[MWIN_PAD_NUMBERED_HATS];
    Take(&down, sizeof(down));
    Take(axes, sizeof(axes));
    Take(hats, sizeof(hats));
    for (int i = 0; i < MWIN_PAD_NUMBERED_BUTTONS; i++)
    {
        controls->buttons[i] = (down >> i & 1u) != 0;
    }
    for (int i = 0; i < MWIN_PAD_NUMBERED_AXES; i++)
    {
        controls->axes[i] = axes[i] < -127 ? -1.0f : (float)axes[i] / 127.0f;
    }
    for (int i = 0; i < MWIN_PAD_NUMBERED_HATS; i++)
    {
        controls->hats[i] = hats[i] & 15u;
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    static mwinPadMapping mapping;
    static mwinPadSource halves[MWIN_PAD_HALVES];
    uint8_t shape[4];
    Take(shape, sizeof(shape));
    Take(mapping.sources, sizeof(mapping.sources));
    Take(halves, sizeof(halves));
    bool mapped = (shape[0] & 1u) != 0;
    mwinPadControls controls = {
        .mapping = mapped ? &mapping : nullptr,
        .halves = (shape[0] & 2u) != 0 ? halves : nullptr,
        .buttonCount = (uint8_t)(shape[1] % (MWIN_PAD_NUMBERED_BUTTONS + 1)),
        .axisCount = (uint8_t)(shape[2] % (MWIN_PAD_NUMBERED_AXES + 1)),
        .hatCount = (uint8_t)(shape[3] % (MWIN_PAD_NUMBERED_HATS + 1)),
    };
    int raw = controls.axisCount + 2 * controls.hatCount;
    mwinGamepadInfo info = {
        .mapped = mapped,
        .rawButtons = controls.buttonCount,
        .rawAxes = (uint8_t)(raw < MWIN_GAMEPAD_RAW_AXES ? raw : MWIN_GAMEPAD_RAW_AXES),
        .battery = -1};
    int32_t slot = mwinAddGamepad(context, &info, 0);
    Expect(slot >= 0);
    while (s_size > 0)
    {
        TakeReport(&controls);
        mwinPostPadControls(context, (uint32_t)slot, &controls, 0);
        Check(context, (uint32_t)slot, &controls);
    }
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_frameStop;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    s_data = data;
    s_size = size;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.init = Init;
    def.frame = Frame;
    Expect(mwinRun(&def) == mwin_success);
    return 0;
}
