// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The object a destroy ends, read back from the program's free hook on
// the test backend: a window whose held icon request gives its copy back
// when the destroy cancels it, and a cursor whose images go back. Every
// read from the hook finds the object gone, and the window's records,
// its request's completion among them, still come.

#include "cursor.h"
#include "test_program.h"

#include "maul-window/input.h"
#include "maul-window/native.h"
#include "maul-window/test.h"
#include "maul-window/window.h"

#include <stdlib.h>

static const uint8_t s_pixels[16 * 16 * 4];
static const mwinIconImage s_image = {16, 16, 16 * 4, s_pixels};

// What the hook reads back, and what it saw.
static mwinContext* s_context;
static mwinWindowId s_window;
static mwinCursorId s_cursor;
static bool s_armed;
static int s_releases;
static int s_found;

static void* HookAllocate(size_t size, size_t alignment, void* user)
{
    (void)user;
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

static void HookFree(void* memory, size_t size, size_t alignment, void* user)
{
    (void)size;
    (void)alignment;
    (void)user;
    free(memory);
    if (!s_armed)
    {
        return;
    }
    s_releases += 1;
    mwinWindowState state;
    mwinNativeHandles handles;
    s_found += s_window.index1 != 0 &&
               (mwinWindow_GetState(s_context, s_window, &state) != mwin_errorStale ||
                mwinWindow_GetNativeHandles(s_context, s_window, &handles) != mwin_errorStale);
    s_found += s_cursor.index1 != 0 && mwinFindCursor(s_context, s_cursor) != nullptr;
}

static bool Has(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        if (program->events[i].type == type)
        {
            return true;
        }
    }
    return false;
}

static bool Cancelled(const Program* program, mwinRequestId request)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        if (event->type == mwin_eventRequestCompleted &&
            SameId(event->data.completion.request, request))
        {
            return event->data.completion.outcome == mwin_outcomeCancelled;
        }
    }
    return false;
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    switch (step)
    {
    case 0:
        s_context = context;
        program->windows[0] = Create(context, &program->requests[0]);
        break;
    case 1:
        CHECK(mwinContext_HoldTestRequests(context, true) == mwin_success &&
                  mwinWindow_RequestIcon(context, program->windows[0], &s_image, 1,
                                         &program->requests[1]) == mwin_success,
              "an icon request held");
        break;
    case 2:
        s_window = program->windows[0];
        s_armed = true;
        CHECK(mwinDestroyWindow(context, s_window) == mwin_success, "the window destroyed");
        s_armed = false;
        CHECK(s_releases > 0, "the destroy gives the icon's copy back through the free hook");
        CHECK(s_found == 0, "the window is gone in the free hook");
        s_window = (mwinWindowId){0};
        CHECK(mwinContext_HoldTestRequests(context, false) == mwin_success, "answer again");
        break;
    case 3:
    {
        CHECK(Cancelled(program, program->requests[1]) && Has(program, mwin_eventWindowDestroyed),
              "the request cancelled and the window's end reported");
        mwinCursorDef def = mwinDefaultCursorDef();
        def.images = &s_image;
        def.imageCount = 1;
        CHECK(mwinCreateCursor(context, &def, &s_cursor) == mwin_success, "a cursor");
        s_releases = 0;
        s_armed = true;
        CHECK(mwinDestroyCursor(context, s_cursor) == mwin_success, "the cursor destroyed");
        s_armed = false;
        CHECK(s_releases == 1, "the destroy gives the images back through the free hook");
        CHECK(s_found == 0, "the cursor is gone in the free hook");
        program->done = true;
        break;
    }
    default:
        break;
    }
}

int main(void)
{
    Program program = {.step = Step};
    mwinContextDef def = mwinDefaultContextDef();
    def.allocator = (mwinAllocator){HookAllocate, HookFree, nullptr};
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
