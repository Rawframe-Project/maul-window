// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What an evdev reader does with each event once the kernel has dropped
// some (SYN_DROPPED, when a reader's queue overflows). The queue then
// holds what is left of the report the drop cut, so, as the kernel's
// evdev documentation says, the reader ignores every event up to and
// including the next SYN_REPORT, and then reads the device's state
// again. It takes plain values; linux_pad.c and linux_motion_events.c
// use it.

#ifndef MAUL_WINDOW_SRC_LINUX_EVDEV_DROP_H
#define MAUL_WINDOW_SRC_LINUX_EVDEV_DROP_H

#include <stdint.h>

typedef enum mwinEvdevStep
{
    // Read the event as usual.
    mwin_evdevTake,
    // Ignore it: it belongs to a report a drop cut.
    mwin_evdevSkip,
    // The report a drop cut has ended: read the device's state again.
    mwin_evdevResync,
} mwinEvdevStep;

// The step for an event, with whether the reader is skipping since a
// drop, which it updates.
mwinEvdevStep mwinEvdevDropStep(bool* dropping, uint16_t type, uint16_t code);

#endif // MAUL_WINDOW_SRC_LINUX_EVDEV_DROP_H
