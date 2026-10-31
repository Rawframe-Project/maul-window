// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Kinds below their range, on the test backend: every enumeration a call
// takes by value, a def holds or a test report carries (a key's code) is
// an int32_t, so -1 and INT32_MIN are values a program can pass. Each
// call refuses both with
// mwin_errorInvalid and counts one misuse on a live context (none
// without one); mwinContext_MapKeyCode, which answers a key, answers
// none. The sets of flags are uint32_t, so all bits set stands for a
// value past every flag.

#include "test_program.h"

#include "maul-window/dialog.h"
#include "maul-window/gamepad.h"
#include "maul-window/input.h"
#include "maul-window/services.h"
#include "maul-window/test.h"
#include "maul-window/window.h"

#include <stdint.h>
#include <stdio.h>

static const int32_t s_below[] = {-1, INT32_MIN};

// Whether a call answered mwin_errorInvalid and counted one misuse.
static void Refused(mwinContext* context, const char* what, int32_t value, mwinResult status,
                    uint64_t misuseBefore)
{
    uint64_t counted = mwinContext_GetMisuse(context) - misuseBefore;
    if (status != mwin_errorInvalid || counted != 1)
    {
        printf("FAIL: %s %ld: answered %d, %llu misuse counted\n", what, (long)value, (int)status,
               (unsigned long long)counted);
        s_failures += 1;
    }
}

#define REFUSED(what, value, call)                                                                 \
    do                                                                                             \
    {                                                                                              \
        uint64_t before = mwinContext_GetMisuse(context);                                          \
        Refused(context, what, value, (call), before);                                             \
    } while (0)

// The calls on a window and the context that take a kind by value.
static void Arguments(mwinContext* context, mwinWindowId window, mwinGamepadId gamepad)
{
    for (size_t i = 0; i < sizeof(s_below) / sizeof(s_below[0]); i++)
    {
        int32_t v = s_below[i];
        REFUSED("a keyboard's purpose", v,
                mwinWindow_RequestVirtualKeyboard(context, window, true, v, nullptr));
        REFUSED("a cursor mode", v, mwinWindow_RequestCursorMode(context, window, v, nullptr));
        REFUSED("a cursor shape", v, mwinWindow_RequestCursorShape(context, window, v, nullptr));
        REFUSED("a window mode", v, mwinWindow_RequestMode(context, window, v, nullptr));
        mwinKeyReach reach = 0;
        REFUSED("a key code's reach", v, mwinContext_GetKeyReach(context, v, 0, &reach));
        CHECK(mwinContext_MapKeyCode(context, v) == 0, "no key for a key code below the range");
        REFUSED("a test answer's kind", v, mwinContext_SetTestAnswer(context, v, mwin_outcomeDone));
        REFUSED("a test answer's outcome", v,
                mwinContext_SetTestAnswer(context, mwin_requestTitle, v));
        REFUSED("a test key reach's code", v,
                mwinContext_SetTestKeyReach(context, v, 0, mwin_keyReachDelivered));
        REFUSED("a test key reach", v, mwinContext_SetTestKeyReach(context, mwin_codeKeyA, 0, v));
        char bytes[8];
        size_t length = 0;
        REFUSED("the kind of what the test platform opened", v,
                mwinContext_GetTestOpened(context, v, bytes, sizeof(bytes), &length));
        mwinEvent event = {.type = v, .window = window};
        REFUSED("a test report's type", v, mwinContext_PostTestEvent(context, &event));
        mwinEvent down = {.type = mwin_eventKeyDown, .window = window};
        down.data.key.code = v;
        REFUSED("a key press's code", v, mwinContext_PostTestEvent(context, &down));
        mwinEvent up = {.type = mwin_eventKeyUp, .window = window};
        up.data.key.code = v;
        REFUSED("a key release's code", v, mwinContext_PostTestEvent(context, &up));
#ifdef MWIN_TEST_GAMEPAD
        REFUSED("a test gamepad's button", v, mwinGamepad_SetTestButton(context, gamepad, v, true));
        REFUSED("a test gamepad's axis", v, mwinGamepad_SetTestAxis(context, gamepad, v, 1.0f));
#else
        (void)gamepad;
#endif
    }
    REFUSED("a window style past every flag", -1,
            mwinWindow_RequestStyle(context, window, UINT32_MAX, nullptr));
    mwinEvent past = {.type = mwin_eventKeyUp, .window = window};
    past.data.key.code = mwin_codeMetaRight + 1;
    REFUSED("a key release's code past the last", past.data.key.code,
            mwinContext_PostTestEvent(context, &past));
}

// The defs that hold a kind.
static void Defs(mwinContext* context, mwinWindowId window)
{
    for (size_t i = 0; i < sizeof(s_below) / sizeof(s_below[0]); i++)
    {
        int32_t v = s_below[i];
        mwinWindowDef def = mwinDefaultWindowDef();
        def.mode = v;
        mwinWindowId made = {0};
        REFUSED("a window def's mode", v, mwinCreateWindow(context, &def, &made, nullptr));
        def = mwinDefaultWindowDef();
        def.kind = v;
        REFUSED("a window def's kind", v, mwinCreateWindow(context, &def, &made, nullptr));
        mwinFileDialogDef dialog = mwinDefaultFileDialogDef();
        dialog.kind = v;
        REFUSED("a dialog def's kind", v,
                mwinWindow_RequestFileDialog(context, window, &dialog, nullptr));
        mwinHitRegion region = {{0.0f, 0.0f, 10.0f, 10.0f}, v};
        REFUSED("a hit region's kind", v,
                mwinWindow_RequestHitRegions(context, window, &region, 1, nullptr));
        // No context: refused, counted nowhere.
        mwinMessageBoxDef box = mwinDefaultMessageBoxDef();
        box.kind = v;
        bool accepted = true;
        CHECK(mwinShowMessageBox(&box, &accepted) == mwin_errorInvalid,
              "a message box def's kind below the range");
        box = mwinDefaultMessageBoxDef();
        box.buttons = v;
        CHECK(mwinShowMessageBox(&box, &accepted) == mwin_errorInvalid,
              "a message box def's buttons below the range");
    }
    mwinWindowDef def = mwinDefaultWindowDef();
    def.style = UINT32_MAX;
    mwinWindowId made = {0};
    REFUSED("a window def's style past every flag", -1,
            mwinCreateWindow(context, &def, &made, nullptr));
}

static mwinGamepadId s_gamepad;

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, &program->requests[0]);
#ifdef MWIN_TEST_GAMEPAD
        {
            mwinGamepadInfo info = {.mapped = true, .battery = -1};
            CHECK(mwinAddTestGamepad(context, &info, &s_gamepad) == mwin_success, "a gamepad");
        }
#endif
        break;
    case 1:
        Arguments(context, program->windows[0], s_gamepad);
        Defs(context, program->windows[0]);
        break;
    case 2:
        // A frame after the refused reports: none of them reaches the
        // queue.
        program->done = true;
        break;
    default:
        break;
    }
}

int main(void)
{
    // A run's context def with its backend below the range: refused
    // before the program's init.
    for (size_t i = 0; i < sizeof(s_below) / sizeof(s_below[0]); i++)
    {
        Program refused = {.step = Step};
        mwinAppDef def = mwinDefaultAppDef();
        def.context.backend = s_below[i];
        def.init = Init;
        def.frame = Frame;
        def.quit = Quit;
        def.user = &refused;
        CHECK(mwinRun(&def) == mwin_errorInvalid && refused.frame == 0,
              "a context def's backend below the range refused");
    }
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
