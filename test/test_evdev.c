// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Linux input event codes as key codes: every code with a key maps back
// to itself; KEY_F24, the table's last, is F24, and the code past it,
// like any larger one, has no key.

#include "evdev.h"
#include "test_harness.h"

int main(void)
{
    bool back = true;
    for (uint32_t evdev = 0; evdev <= 194; evdev++)
    {
        mwinKeyCode code = mwinKeyCodeFromEvdev(evdev);
        back = back && (code == mwin_codeUnknown || mwinEvdevFromKeyCode(code) == evdev);
    }
    CHECK(back, "every code with a key maps back to itself");
    CHECK(mwinKeyCodeFromEvdev(194) == mwin_codeF24 && mwinEvdevFromKeyCode(mwin_codeF24) == 194,
          "KEY_F24 is F24");
    CHECK(mwinKeyCodeFromEvdev(195) == mwin_codeUnknown &&
              mwinKeyCodeFromEvdev(UINT32_MAX) == mwin_codeUnknown,
          "no key past the table");
    return s_failures == 0 ? 0 : 1;
}
