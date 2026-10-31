// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A run's teardown read back from the program's free hook on the test
// backend: armed once quit has run, the hook reads a window, a cursor,
// the payloads of a clipboard read of each kind, a dialog and a drop, and
// the context, at every block the teardown gives back but the context's
// own, the cursor's images, the payloads, the platform's clipboard and
// the platform block among them. Every id and drop number is stale at
// each, and the calls that would ask the backend without an id answer
// mwin_errorState, or no key.

#include "cursor.h"
#include "test_program.h"

#include "maul-window/clipboard.h"
#include "maul-window/dialog.h"
#include "maul-window/drop.h"
#include "maul-window/input.h"
#include "maul-window/native.h"
#include "maul-window/test.h"
#include "maul-window/window.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define ROOT "C:"
#else
#define ROOT ""
#endif

static const uint8_t s_pixels[16 * 16 * 4];
static const mwinIconImage s_image = {16, 16, 16 * 4, s_pixels};
static const char s_files[] = ROOT "/a";

// What the hook reads back, and what it saw.
static mwinContext* s_context;
static mwinWindowId s_window;
static mwinCursorId s_cursor;
static mwinRequestId s_text;
static mwinRequestId s_data;
static mwinRequestId s_primary;
static mwinRequestId s_dialog;
static uint32_t s_drop;
static bool s_armed;
static int s_releases;
static int s_found;

// Whether the reads of the payloads answer: all of them live in quit,
// none from the hook. Counts each that does not answer as it should.
static int Payloads(bool live)
{
    char bytes[16];
    size_t length = 0;
    uint32_t count = 0;
    mwinResult want = live ? mwin_success : mwin_errorStale;
    int wrong = 0;
    wrong += mwinRequest_GetClipboardText(s_context, s_text, bytes, sizeof(bytes), &length) != want;
    wrong += mwinRequest_GetClipboardData(s_context, s_data, bytes, sizeof(bytes), &length) != want;
    wrong +=
        mwinRequest_GetPrimaryText(s_context, s_primary, bytes, sizeof(bytes), &length) != want;
    wrong += mwinRequest_GetDialogFiles(s_context, s_dialog, bytes, sizeof(bytes), &length,
                                        &count) != want;
    wrong += mwinContext_GetDroppedFiles(s_context, s_drop, bytes, sizeof(bytes), &length) != want;
    wrong += mwinContext_GetDroppedText(s_context, s_drop, bytes, sizeof(bytes), &length) != want;
    return wrong;
}

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
    memset(layout, 'x', sizeof(layout));
    size_t length = 0;
    s_found += mwinWindow_GetState(s_context, s_window, &state) != mwin_errorStale;
    s_found += mwinWindow_GetNativeHandles(s_context, s_window, &handles) != mwin_errorStale;
    s_found += mwinFindCursor(s_context, s_cursor) != nullptr;
    s_found += Payloads(false);
    s_found += mwinContext_GetKeyReach(s_context, mwin_codeKeyA, 0, &reach) != mwin_errorState;
    s_found += mwinContext_GetKeyboardLayout(s_context, layout, sizeof(layout), &length) !=
               mwin_errorState;
    static const char zeros[sizeof(layout)];
    s_found += memcmp(layout, zeros, sizeof(layout)) != 0 || length != 0;
    s_found += mwinContext_MapKeyCode(s_context, mwin_codeKeyA) != 0;
    // No id names the platform's clipboard: its text, or none, or no
    // platform; never another block's bytes.
    char held[16];
    mwinResult status = mwinContext_GetTestClipboard(s_context, held, sizeof(held), &length);
    s_found += status == mwin_success && length != 0 && (length != 4 || memcmp(held, "held", 4));
}

static uint32_t DropNumber(const Program* program)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        if (program->events[i].type == mwin_eventDropped)
        {
            return program->events[i].data.drop.drop;
        }
    }
    return 0;
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinWindowId window = program->windows[0];
    switch (step)
    {
    case 0:
        s_context = context;
        program->windows[0] = Create(context, &program->requests[0]);
        break;
    case 1:
    {
        s_window = window;
        mwinCursorDef def = mwinDefaultCursorDef();
        def.images = &s_image;
        def.imageCount = 1;
        CHECK(mwinCreateCursor(context, &def, &s_cursor) == mwin_success, "a cursor");
        // Data first: another program's data takes the platform's text.
        CHECK(mwinContext_SetTestClipboardData(context, "image/png", 9, "PNG1", 4) ==
                      mwin_success &&
                  mwinWindow_RequestClipboardReadData(context, window, "image/png", 9, &s_data) ==
                      mwin_success,
              "a read of data");
        break;
    }
    case 2:
    {
        CHECK(mwinContext_SetTestClipboard(context, "held", 4) == mwin_success &&
                  mwinWindow_RequestClipboardRead(context, window, &s_text) == mwin_success,
              "a read of text, the platform's clipboard holding a block");
        CHECK(mwinContext_SetTestPrimary(context, "One", 3) == mwin_success &&
                  mwinWindow_RequestPrimaryRead(context, window, &s_primary) == mwin_success,
              "a read of the primary selection");
        mwinFileDialogDef dialog = mwinDefaultFileDialogDef();
        dialog.kind = mwin_dialogOpen;
        CHECK(mwinContext_SetTestDialogFiles(context, s_files, sizeof(s_files)) == mwin_success &&
                  mwinWindow_RequestFileDialog(context, window, &dialog, &s_dialog) == mwin_success,
              "a dialog");
        CHECK(mwinWindow_PostTestDrop(context, window, (mwinPosition){1.0f, 1.0f}, s_files,
                                      sizeof(s_files), "one", 3) == mwin_success,
              "a drop");
        break;
    }
    case 3:
        s_drop = DropNumber(program);
        CHECK(s_drop != 0, "the drop delivered");
        CHECK(Payloads(true) == 0, "every payload read back during the run");
        program->done = true;
        break;
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
    CHECK(Payloads(true) == 0, "every payload read back in quit");
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
    // The cursor's images, the read text, data and primary text, the
    // dialog's files, the drop's files and text, the platform's clipboard
    // and the platform block, at least.
    CHECK(s_releases >= 10, "the teardown gives its blocks back through the free hook");
    CHECK(s_found == 0, "no window, cursor, payload or backend reached from the free hook");
    return s_failures == 0 ? 0 : 1;
}
