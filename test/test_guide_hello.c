// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's first snippet (docs/guide.md, "The model"), a whole
// program as written there: built with the tests, warnings as errors,
// and not run, since it waits for its user to close the window
// (tools/check_guide.py checks that it is here unchanged).

#include "maul-window/event.h"
#include "maul-window/window.h"

static mwinWindowId s_window;

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Hello";
    def.titleLength = 5;
    def.size = (mwinSize){800.0f, 600.0f};
    return mwinCreateWindow(context, &def, &s_window, NULL);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    (void)user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventCloseRequested)
        {
            return mwin_frameStop;
        }
    }
    // Draw the frame here.
    return mwin_frameContinue;
}

int main(void)
{
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    return mwinRun(&def) == mwin_success ? 0 : 1;
}
