// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the Android test applications share: the activity a window shows
// in, taken only while the window has its surface.

#ifndef MAUL_WINDOW_TEST_ANDROID_APP_H
#define MAUL_WINDOW_TEST_ANDROID_APP_H

#include "test_harness.h"

#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <string.h>

// The activity a window shows in, or NULL while the window has no
// surface or no handles. A frame between an activity's start and its
// surface, which the start itself runs (a turn that relaunches the
// activity, a picker that stops it), gets no handles: the library
// answers mwin_errorState and gives out no pointer, which this checks,
// so a test that keeps the activity never keeps NULL.
static inline ANativeActivity* ActivityWithSurface(mwinContext* context, mwinWindowId window)
{
    mwinWindowState state = {0};
    (void)mwinWindow_GetState(context, window, &state);
    mwinNativeHandles handles;
    memset(&handles, 0xA5, sizeof(handles));
    mwinResult status = mwinWindow_GetNativeHandles(context, window, &handles);
    if (state.surfaceLost)
    {
        static const mwinNativeHandles none = {0};
        CHECK(status == mwin_errorState && memcmp(&handles, &none, sizeof(handles)) == 0,
              "no handles while the surface is gone");
        return nullptr;
    }
    return status == mwin_success && handles.handles.android.window != nullptr
               ? handles.handles.android.activity
               : nullptr;
}

// Whether the window has its surface and the activity's handles.
static inline bool HasSurface(mwinContext* context, mwinWindowId window)
{
    return ActivityWithSurface(context, window) != nullptr;
}

#endif
