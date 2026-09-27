// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Owned and popup windows on the test backend: defs refused (a popup
// without an owner, not windowed, at no finite place, an unknown kind)
// and an owner that no longer exists; a popup placed against its
// owner, its position in its state, its modes and styles unsupported;
// and destroying an owner destroys what it owns first, the popup of a
// dialog before the dialog, each with its record.

#include "test_program.h"

#include "maul-window/test.h"

#include <math.h>

static mwinWindowDef Popup(mwinWindowId owner)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){120.0f, 80.0f};
    def.owner = owner;
    def.kind = mwin_windowMenu;
    def.position = (mwinPosition){10.0f, 20.0f};
    return def;
}

static bool Refused(mwinContext* context, mwinWindowDef def, mwinResult expected)
{
    mwinWindowId window = {0};
    return mwinCreateWindow(context, &def, &window, nullptr) == expected;
}

static void CheckRefusals(mwinContext* context, mwinWindowId owner)
{
    mwinWindowDef orphan = Popup((mwinWindowId){0});
    mwinWindowDef maximized = Popup(owner);
    maximized.mode = mwin_modeMaximized;
    mwinWindowDef nowhere = Popup(owner);
    nowhere.position.x = NAN;
    mwinWindowDef unknown = Popup(owner);
    unknown.kind = 3;
    mwinWindowDef gone = Popup((mwinWindowId){owner.index1, owner.generation + 1});
    CHECK(
        Refused(context, orphan, mwin_errorInvalid) &&
            Refused(context, maximized, mwin_errorInvalid) &&
            Refused(context, nowhere, mwin_errorInvalid) &&
            Refused(context, unknown, mwin_errorInvalid) && Refused(context, gone, mwin_errorStale),
        "popups without an owner, not windowed or nowhere, unknown kinds and gone owners refused");
}

static int Outcome(const Program* program, int request)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        if (event->type == mwin_eventRequestCompleted &&
            SameId(event->data.completion.request, program->requests[request]))
        {
            return event->data.completion.outcome;
        }
    }
    return -1;
}

// The order windows were destroyed in, by their index in the program.
static int DestroyedOrder(const Program* program, int* order, int capacity)
{
    int count = 0;
    for (int i = 0; i < program->eventCount && count < capacity; i++)
    {
        const mwinEvent* event = &program->events[i];
        for (int w = 0; w < 4 && event->type == mwin_eventWindowDestroyed; w++)
        {
            if (SameWindow(event->window, program->windows[w]))
            {
                order[count++] = w;
            }
        }
    }
    return count;
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinWindowState state;
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        break;
    case 1:
    {
        CheckRefusals(context, program->windows[0]);
        mwinWindowDef dialog = mwinDefaultWindowDef();
        dialog.owner = program->windows[0];
        mwinWindowDef popup = Popup(program->windows[0]);
        CHECK(mwinCreateWindow(context, &dialog, &program->windows[1], nullptr) == mwin_success &&
                  mwinCreateWindow(context, &popup, &program->windows[2], nullptr) == mwin_success,
              "a dialog and a popup of the window");
        break;
    }
    case 2:
    {
        mwinWindowDef popup = Popup(program->windows[1]);
        CHECK(mwinGetWindowState(context, program->windows[2], &state) == mwin_success &&
                  state.position.x == 10.0f && state.position.y == 20.0f,
              "a popup placed against its owner, its position in its state");
        CHECK(mwinRequestMode(context, program->windows[2], mwin_modeMaximized,
                              &program->requests[0]) == mwin_success &&
                  mwinRequestStyle(context, program->windows[2], mwin_styleDecorated,
                                   &program->requests[1]) == mwin_success &&
                  mwinCreateWindow(context, &popup, &program->windows[3], nullptr) == mwin_success,
              "ask a popup for a mode and a style; a popup of the dialog");
        break;
    }
    case 3:
        CHECK(Outcome(program, 0) == mwin_outcomeUnsupported &&
                  Outcome(program, 1) == mwin_outcomeUnsupported,
              "a popup's modes and styles unsupported");
        program->eventCount = 0;
        CHECK(mwinDestroyWindow(context, program->windows[0]) == mwin_success, "destroy");
        break;
    default:
    {
        int order[4];
        int count = DestroyedOrder(program, order, 4);
        CHECK(count == 4 && order[0] == 3 && order[1] == 1 && order[2] == 2 && order[3] == 0 &&
                  mwinGetWindowState(context, program->windows[3], &state) == mwin_errorStale,
              "destroying an owner destroys what it owns first, theirs before them");
        program->done = true;
        break;
    }
    }
}

int main(void)
{
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
