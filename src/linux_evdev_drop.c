// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An evdev reader's steps after the kernel drops events.

#include "linux_evdev_drop.h"

#include <linux/input.h>

mwinEvdevStep mwinEvdevDropStep(bool* dropping, uint16_t type, uint16_t code)
{
    if (type == EV_SYN && code == SYN_DROPPED)
    {
        *dropping = true;
        return mwin_evdevSkip;
    }
    if (!*dropping)
    {
        return mwin_evdevTake;
    }
    if (type == EV_SYN && code == SYN_REPORT)
    {
        *dropping = false;
        return mwin_evdevResync;
    }
    return mwin_evdevSkip;
}
