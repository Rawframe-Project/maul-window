// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X server an X11 test talks to: the one DISPLAY names, which the
// display fixture starts for every run. A test is skipped only where
// DISPLAY names no server. One that cannot reach the server DISPLAY
// names fails with the reason: the fixture's server is always there, so
// a skip would only hide a broken run.

#ifndef MAUL_WINDOW_TEST_X11_DISPLAY_H
#define MAUL_WINDOW_TEST_X11_DISPLAY_H

#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>

// Exits skipped (77) where DISPLAY names no X server.
static inline void DisplayRequired(void)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        // hygiene: no X server here is a skip, and CI requires one
        exit(77);
    }
}

// Exits failing, with the reason, where a test cannot go on.
static inline void DisplayFail(const char* reason)
{
    printf("FAIL: %s (DISPLAY=%s)\n", reason, getenv("DISPLAY"));
    exit(1);
}

// A connection to the X server DISPLAY names, or exits failing with
// xcb's error.
static inline xcb_connection_t* DisplayConnect(void)
{
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    int error = xcb_connection_has_error(connection);
    if (error != 0)
    {
        printf("FAIL: cannot connect to the X server (DISPLAY=%s, xcb error %d)\n",
               getenv("DISPLAY"), error);
        exit(1);
    }
    return connection;
}

#endif // MAUL_WINDOW_TEST_X11_DISPLAY_H
