// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A run's teardown read back from the program's free hook on the test
// backend: armed once quit has run, the hook reads a window, a cursor and
// the context at every block the teardown gives back but the context's
// own, the cursor's images, the platform's clipboard and the platform
// block among them. The window and the cursor are gone at each, and the
// calls that would ask the backend without an id answer mwin_errorState,
// or no key.

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
    // The context's own block is the last; nothing reads it after.
    bool read = s_armed && memory != (void*)s_context;
    free(memory);
    if (!read)
    {
        return;
    }
    s_releases += 1;
    mwinWindowState state;
    mwinNativeHandles handles;
    mwinKeyReach reach;
    char layout[64];
    size_t length = 0;
    s_found += mwinWindow_GetState(s_context, s_window, &state) != mwin_errorStale;
    s_found += mwinWindow_GetNativeHandles(s_context, s_window, &handles) != mwin_errorStale;
    s_found += mwinFindCursor(s_context, s_cursor) != nullptr;
    s_found += mwinContext_GetKeyReach(s_context, mwin_codeKeyA, 0, &reach) != mwin_errorState;
    s_found += mwinContext_GetKeyboardLayout(s_context, layout, sizeof(layout), &length) !=
               mwin_errorState;
    s_found += mwinContext_MapKeyCode(s_context, mwin_codeKeyA) != 0;
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
    {
        s_window = program->windows[0];
        mwinCursorDef def = mwinDefaultCursorDef();
        def.images = &s_image;
        def.imageCount = 1;
        CHECK(mwinCreateCursor(context, &def, &s_cursor) == mwin_success, "a cursor");
        static const char text[] = "held";
        CHECK(mwinContext_SetTestClipboard(context, text, sizeof(text) - 1) == mwin_success,
              "the platform's clipboard holds a block");
        program->done = true;
        break;
    }
    default:
        break;
    }
}

// Quit still reads them back, then arms the hook for the teardown.
static void ArmInQuit(mwinContext* context, mwinResult status, void* user)
{
    (void)status;
    (void)user;
    mwinWindowState state;
    CHECK(mwinWindow_GetState(context, s_window, &state) == mwin_success &&
              mwinFindCursor(context, s_cursor) != nullptr,
          "the window and the cursor exist in quit");
    s_armed = true;
}

int main(void)
{
    Program program = {.step = Step};
    mwinAppDef def = mwinDefaultAppDef();
    def.context.allocator = (mwinAllocator){HookAllocate, HookFree, nullptr};
    def.context.backend = mwin_backendTest;
    def.init = Init;
    def.frame = Frame;
    def.quit = ArmInQuit;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs");
    CHECK(s_releases >= 3, "the teardown gives back the cursor's images, the clipboard and the "
                           "platform block through the free hook");
    CHECK(s_found == 0, "no window, cursor or backend reached from the free hook");
    return s_failures == 0 ? 0 : 1;
}
