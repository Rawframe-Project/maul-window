// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An evdev reader's steps after the kernel drops events, as plain values
// (linux_evdev_drop.h), on Linux without a device:
// - events before a drop taken; every event after it skipped, another
//   drop among them; the next report ending the skip with a resync; the
//   reports after it taken;
// - a resync's buttons each from the bit of its key code, a code past
//   the key state read as up; a state that could not be read setting
//   nothing and giving nothing to post.

#include "linux_evdev_drop.h"
#include "test_harness.h"

#include <limits.h>
#include <string.h>

#define LONG_BITS (sizeof(unsigned long) * CHAR_BIT)

static void SetKey(unsigned long* keys, unsigned code)
{
    keys[code / LONG_BITS] |= 1ul << (code % LONG_BITS);
}

static void TestDropGate(void)
{
    bool dropping = false;
    CHECK(mwinEvdevDropStep(&dropping, EV_ABS, ABS_X) == mwin_evdevTake &&
              mwinEvdevDropStep(&dropping, EV_SYN, SYN_REPORT) == mwin_evdevTake,
          "events before a drop read as usual");
    CHECK(mwinEvdevDropStep(&dropping, EV_SYN, SYN_DROPPED) == mwin_evdevSkip &&
              mwinEvdevDropStep(&dropping, EV_KEY, BTN_SOUTH) == mwin_evdevSkip &&
              mwinEvdevDropStep(&dropping, EV_ABS, ABS_X) == mwin_evdevSkip &&
              mwinEvdevDropStep(&dropping, EV_MSC, MSC_TIMESTAMP) == mwin_evdevSkip &&
              mwinEvdevDropStep(&dropping, EV_SYN, SYN_DROPPED) == mwin_evdevSkip,
          "every event after a drop skipped, another drop among them");
    CHECK(mwinEvdevDropStep(&dropping, EV_SYN, SYN_REPORT) == mwin_evdevResync,
          "the next report ending the skip, with a resync");
    CHECK(mwinEvdevDropStep(&dropping, EV_KEY, BTN_SOUTH) == mwin_evdevTake &&
              mwinEvdevDropStep(&dropping, EV_SYN, SYN_REPORT) == mwin_evdevTake,
          "the reports after it read as usual");
}

static void TestResyncButtons(void)
{
    const uint16_t codes[] = {BTN_SOUTH, BTN_EAST, BTN_TL, KEY_MAX, KEY_CNT};
    unsigned long keys[MWIN_EVDEV_KEY_LONGS] = {0};
    SetKey(keys, BTN_SOUTH);
    SetKey(keys, BTN_TL);
    SetKey(keys, KEY_MAX);
    bool buttons[5] = {false, true, false, false, true};
    CHECK(mwinEvdevResyncButtons(keys, true, codes, 5, buttons) && buttons[0] && !buttons[1] &&
              buttons[2] && buttons[3] && !buttons[4],
          "each button from its key code's bit, the last key code too, a code past them up");

    const bool held[5] = {true, true, false, true, false};
    bool unread[5];
    memcpy(unread, held, sizeof(unread));
    unsigned long none[MWIN_EVDEV_KEY_LONGS] = {0};
    CHECK(!mwinEvdevResyncButtons(none, false, codes, 5, unread) &&
              memcmp(unread, held, sizeof(held)) == 0,
          "a state that could not be read: nothing to post, the buttons as they were");
    CHECK(mwinEvdevResyncButtons(none, true, codes, 0, unread), "a pad without buttons");
}

int main(void)
{
    TestDropGate();
    TestResyncButtons();
    return s_failures == 0 ? 0 : 1;
}
