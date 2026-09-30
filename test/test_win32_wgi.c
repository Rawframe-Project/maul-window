// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Windows.Gaming.Input itself (src/win32_wgi.c), where the system has it
// (not every edition of Windows does, nor wine without it): it starts,
// lists what it has with each pad's name, and stops. Its pads' tracking
// runs against a stand-in in pad_tracker.

#include "test_harness.h"
#include "win32_wgi.h"

int main(void)
{
    mwinWgi wgi;
    mwinPadRuntime runtime;
    if (!mwinWgiStart(&wgi, &runtime))
    {
        (void)printf("no Windows.Gaming.Input here\n");
        return 0;
    }
    void* pads[MWIN_PAD_TRACKER_PADS];
    int32_t count = runtime.list(runtime.self, pads, MWIN_PAD_TRACKER_PADS);
    CHECK(count >= 0, "the runtime lists its pads");
    for (int32_t i = 0; i < count; i++)
    {
        mwinGamepadInfo info = {0};
        runtime.describe(runtime.self, pads[i], &info);
        CHECK(info.nameLength > 0 && info.nameLength <= MWIN_GAMEPAD_NAME_BYTES &&
                  (info.capabilities & mwin_padRumble) != 0,
              "a pad's name and motors");
        runtime.release(runtime.self, pads[i]);
    }
    (void)runtime.changed(runtime.self);
    mwinWgiStop(&wgi);
    return s_failures == 0 ? 0 : 1;
}
