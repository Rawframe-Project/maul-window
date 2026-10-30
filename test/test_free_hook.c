// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Getters called from the program's free hook while a payload is
// replaced, on the test backend: a clipboard read's text, data and
// primary text replaced by a later read, a dialog's files by a later
// dialog, a drop by the next drop, and the test platform's clipboard by
// another. The hook frees each block first, then reads the one replaced
// back: it reads the old bytes while they are still the answer, and
// mwin_errorStale, or for the test platform no bytes, once they are not;
// never the block it freed.

#include "test_program.h"

#include "maul-window/clipboard.h"
#include "maul-window/dialog.h"
#include "maul-window/drop.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define ROOT "C:"
#else
#define ROOT ""
#endif

typedef enum Case
{
    CASE_NONE,
    CASE_TEXT,
    CASE_DATA,
    CASE_PRIMARY,
    CASE_DIALOG,
    CASE_DROP,
    CASE_TEST_CLIPBOARD,
} Case;

static const char s_firstFiles[] = ROOT "/a";
static const char s_secondFiles[] = ROOT "/b";

// What the hook reads back, and what it saw.
static mwinContext* s_context;
static Case s_case;
static mwinRequestId s_first;
static uint32_t s_firstDrop;
static int s_live;
static int s_stale;
static int s_wrong;

// Whether bytes read are the expected ones.
static bool Same(const char* bytes, size_t length, const char* expected, size_t expectedLength)
{
    return length == expectedLength && memcmp(bytes, expected, length) == 0;
}

// Counts what a getter answered: the old bytes, stale, or anything else.
static void Count(mwinResult status, bool same)
{
    if (status == mwin_success && same)
    {
        s_live += 1;
    }
    else if (status == mwin_errorStale)
    {
        s_stale += 1;
    }
    else
    {
        s_wrong += 1;
    }
}

static void ReadBack(void)
{
    char bytes[16];
    size_t length = 0;
    uint32_t count = 0;
    mwinResult status = mwin_errorState;
    switch (s_case)
    {
    case CASE_TEXT:
        status = mwinRequest_GetClipboardText(s_context, s_first, bytes, sizeof(bytes), &length);
        Count(status, Same(bytes, length, "First", 5));
        break;
    case CASE_DATA:
        status = mwinRequest_GetClipboardData(s_context, s_first, bytes, sizeof(bytes), &length);
        Count(status, Same(bytes, length, "PNG1", 4));
        break;
    case CASE_PRIMARY:
        status = mwinRequest_GetPrimaryText(s_context, s_first, bytes, sizeof(bytes), &length);
        Count(status, Same(bytes, length, "One", 3));
        break;
    case CASE_DIALOG:
        status =
            mwinRequest_GetDialogFiles(s_context, s_first, bytes, sizeof(bytes), &length, &count);
        Count(status, count == 1 && Same(bytes, length, s_firstFiles, sizeof(s_firstFiles)));
        break;
    case CASE_DROP:
        status = mwinContext_GetDroppedText(s_context, s_firstDrop, bytes, sizeof(bytes), &length);
        Count(status, Same(bytes, length, "one", 3));
        status = mwinContext_GetDroppedFiles(s_context, s_firstDrop, bytes, sizeof(bytes), &length);
        Count(status, Same(bytes, length, s_firstFiles, sizeof(s_firstFiles)));
        break;
    case CASE_TEST_CLIPBOARD:
        // No id names the platform's clipboard: the old text, or none.
        status = mwinContext_GetTestClipboard(s_context, bytes, sizeof(bytes), &length);
        if (status == mwin_success && length == 0)
        {
            s_stale += 1;
        }
        else
        {
            Count(status, Same(bytes, length, "First", 5));
        }
        break;
    case CASE_NONE:
        break;
    }
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
    free(memory);
    ReadBack();
}

// Starts reading back in the hook.
static void Arm(Case kind)
{
    s_case = kind;
    s_live = 0;
    s_stale = 0;
    s_wrong = 0;
}

// Stops it and checks what the hook read.
static void Disarm(const char* what)
{
    s_case = CASE_NONE;
    if (s_stale == 0 || s_wrong > 0)
    {
        printf("FAIL: %s: %d reads of the old bytes, %d stale, %d other\n", what, s_live, s_stale,
               s_wrong);
        s_failures += 1;
    }
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

static mwinFileDialogDef Dialog(void)
{
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = mwin_dialogOpen;
    return def;
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinWindowId window = program->windows[0];
    mwinRequestId request = {0};
    mwinFileDialogDef dialog = Dialog();
    switch (step)
    {
    case 0:
        s_context = context;
        program->windows[0] = Create(context, &program->requests[0]);
        break;
    case 1:
        CHECK(mwinContext_SetTestClipboard(context, "First", 5) == mwin_success &&
                  mwinWindow_RequestClipboardRead(context, window, &s_first) == mwin_success,
              "a first read of text");
        break;
    case 2:
        CHECK(mwinContext_SetTestClipboard(context, "Second", 6) == mwin_success, "new text");
        Arm(CASE_TEXT);
        CHECK(mwinWindow_RequestClipboardRead(context, window, &request) == mwin_success,
              "a second read of text");
        break;
    case 3:
        Disarm("a read's text replaced");
        CHECK(mwinContext_SetTestClipboardData(context, "image/png", 9, "PNG1", 4) ==
                      mwin_success &&
                  mwinWindow_RequestClipboardReadData(context, window, "image/png", 9, &s_first) ==
                      mwin_success,
              "a first read of data");
        break;
    case 4:
        CHECK(mwinContext_SetTestClipboardData(context, "image/png", 9, "PNG2", 4) == mwin_success,
              "new data");
        Arm(CASE_DATA);
        CHECK(mwinWindow_RequestClipboardReadData(context, window, "image/png", 9, &request) ==
                  mwin_success,
              "a second read of data");
        break;
    case 5:
        Disarm("a read's data replaced");
        CHECK(mwinContext_SetTestPrimary(context, "One", 3) == mwin_success &&
                  mwinWindow_RequestPrimaryRead(context, window, &s_first) == mwin_success,
              "a first read of the primary selection");
        break;
    case 6:
        CHECK(mwinContext_SetTestPrimary(context, "Two", 3) == mwin_success, "a new selection");
        Arm(CASE_PRIMARY);
        CHECK(mwinWindow_RequestPrimaryRead(context, window, &request) == mwin_success,
              "a second read of the primary selection");
        break;
    case 7:
        Disarm("a read's primary text replaced");
        CHECK(mwinContext_SetTestDialogFiles(context, s_firstFiles, sizeof(s_firstFiles)) ==
                      mwin_success &&
                  mwinWindow_RequestFileDialog(context, window, &dialog, &s_first) == mwin_success,
              "a first dialog");
        break;
    case 8:
        CHECK(mwinContext_SetTestDialogFiles(context, s_secondFiles, sizeof(s_secondFiles)) ==
                  mwin_success,
              "new files");
        Arm(CASE_DIALOG);
        CHECK(mwinWindow_RequestFileDialog(context, window, &dialog, &request) == mwin_success,
              "a second dialog");
        break;
    case 9:
        Disarm("a dialog's files replaced");
        CHECK(mwinWindow_PostTestDrop(context, window, (mwinPosition){1.0f, 1.0f}, s_firstFiles,
                                      sizeof(s_firstFiles), "one", 3) == mwin_success,
              "a first drop");
        break;
    case 10:
        s_firstDrop = DropNumber(program);
        CHECK(s_firstDrop != 0, "the first drop delivered");
        Arm(CASE_DROP);
        CHECK(mwinWindow_PostTestDrop(context, window, (mwinPosition){1.0f, 1.0f}, s_secondFiles,
                                      sizeof(s_secondFiles), "two", 3) == mwin_success,
              "a second drop");
        break;
    case 11:
        CHECK(DropNumber(program) == s_firstDrop + 1, "the second drop delivered");
        Disarm("a drop replaced");
        CHECK(mwinContext_SetTestClipboard(context, "First", 5) == mwin_success,
              "the platform's text");
        Arm(CASE_TEST_CLIPBOARD);
        CHECK(mwinContext_SetTestClipboard(context, "Second", 6) == mwin_success,
              "the platform's text replaced");
        Disarm("the platform's text replaced");
        program->done = true;
        break;
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
