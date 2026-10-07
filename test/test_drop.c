// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drag and drop's contract on the test backend: a drag's records in
// order with where it is and what it carries; a drop delivering its
// UTF-8 paths, each ended by a NUL, a path that is not UTF-8 left out
// and the drop marked truncated, and its text repaired; a drag that
// leaves; files past droppedFiles and paths past dropBytes left out
// whole, text past dropBytes left out; a later drop making an earlier
// number stale; a buffer that fits exactly, a drop without text, and
// text of dropBytes exactly kept whole; and a drop on a window that
// went, delivered to no one.

#include "test_program.h"

#include "maul-window/drop.h"
#include "maul-window/test.h"

#include <string.h>

#define FILES 3
#define BYTES 32

static void Drag(mwinContext* context, mwinWindowId window, mwinEventType type, float x, float y)
{
    mwinEvent event = {.type = type, .window = window};
    event.data.drag = (mwinDragEvent){{x, y}, mwin_dragFiles | mwin_dragText};
    CHECK(mwinTestPost(context, &event) == mwin_success, "a drag report");
}

static bool DragAt(const mwinEvent* event, mwinEventType type, float x, float y)
{
    return event->type == type && event->data.drag.position.x == x &&
           event->data.drag.position.y == y &&
           event->data.drag.contents == (mwin_dragFiles | mwin_dragText);
}

static bool Copied(mwinContext* context, uint32_t drop, bool text, const char* expected,
                   size_t length)
{
    char bytes[64];
    size_t found = 0;
    mwinResult status = text ? mwinGetDroppedText(context, drop, bytes, sizeof(bytes), &found)
                             : mwinGetDroppedFiles(context, drop, bytes, sizeof(bytes), &found);
    return status == mwin_success && found == length && memcmp(bytes, expected, length) == 0;
}

static void CheckDrop(Program* program, mwinContext* context)
{
    CHECK(program->eventCount == 4 &&
              DragAt(&program->events[0], mwin_eventDragEntered, 10.0f, 20.0f) &&
              DragAt(&program->events[1], mwin_eventDragMoved, 30.0f, 40.0f) &&
              DragAt(&program->events[2], mwin_eventDragMoved, 50.0f, 60.0f) &&
              program->events[3].type == mwin_eventDropped,
          "the drag's records in order, then the drop");
    const mwinDropEvent* drop = &program->events[3].data.drop;
    CHECK(drop->position.x == 50.0f && drop->fileCount == 2 && drop->textLength == 6 &&
              drop->truncated,
          "two files, a path that is not UTF-8 left out, the text");
    CHECK(Copied(context, drop->drop, false, "/a/b.txt\0/\xC3\xA9.png\0", 17) &&
              Copied(context, drop->drop, true, "hi\xEF\xBF\xBD(", 6),
          "the paths each ended by a NUL, and the text repaired");
    char two[2];
    size_t length = 0;
    CHECK(mwinGetDroppedFiles(context, drop->drop, two, sizeof(two), &length) ==
                  mwin_errorCapacity &&
              length == 17 && two[0] == '/' &&
              mwinGetDroppedFiles(context, drop->drop + 1, two, 2, &length) == mwin_errorStale &&
              mwinGetDroppedText(context, 0, nullptr, 0, &length) == mwin_errorStale &&
              mwinGetDroppedText(context, drop->drop, nullptr, 1, &length) == mwin_errorInvalid,
          "the bytes that fit, a number not the last drop's, and a NULL buffer");
    program->requests[1].index1 = drop->drop;
    Drag(context, program->windows[0], mwin_eventDragEntered, 10.0f, 20.0f);
    Drag(context, program->windows[0], mwin_eventDragLeft, 10.0f, 20.0f);
}

static void CheckLeft(Program* program, mwinContext* context)
{
    CHECK(program->eventCount == 2 && program->events[0].type == mwin_eventDragEntered &&
              program->events[1].type == mwin_eventDragLeft,
          "a drag that leaves");
    static const char files[] = "/f1\0/f2\0/f3\0/f4\0";
    CHECK(mwinTestDrop(context, program->windows[0], (mwinPosition){1.0f, 2.0f}, files,
                       sizeof(files) - 1, "0123456789abcdefghijklmnopqrstuvw",
                       33) == mwin_success &&
              mwinTestDrop(context, program->windows[0], (mwinPosition){1.0f, 2.0f}, nullptr, 0,
                           nullptr, 0) == mwin_errorState,
          "a drop past the limits, and no second while it waits");
}

static void CheckLimits(Program* program, mwinContext* context)
{
    const mwinDropEvent* drop = &program->events[0].data.drop;
    CHECK(program->eventCount == 1 && drop->fileCount == FILES && drop->textLength == 0 &&
              drop->truncated && Copied(context, drop->drop, false, "/f1\0/f2\0/f3\0", 12),
          "files past droppedFiles and text past dropBytes left out");
    char twelve[12];
    char four[4];
    size_t length = 0;
    CHECK(
        mwinGetDroppedFiles(context, drop->drop, twelve, sizeof(twelve), &length) == mwin_success &&
            length == 12 &&
            mwinGetDroppedText(context, drop->drop, four, sizeof(four), &length) == mwin_success &&
            length == 0,
        "a buffer that fits exactly, and no text to copy");
    CHECK(mwinGetDroppedFiles(context, program->requests[1].index1, nullptr, 0, &(size_t){0}) ==
              mwin_errorStale,
          "a later drop makes the earlier number stale");
    static const char files[] = "/0123456789\0/abcdefghij\0/klmnopqrst\0";
    CHECK(mwinTestDrop(context, program->windows[0], (mwinPosition){0}, files, sizeof(files) - 1,
                       nullptr, 0) == mwin_success,
          "a drop past the path bytes");
}

static void CheckBytes(Program* program, mwinContext* context)
{
    const mwinDropEvent* drop = &program->events[0].data.drop;
    CHECK(program->eventCount == 1 && drop->fileCount == 2 && drop->truncated &&
              Copied(context, drop->drop, false, "/0123456789\0/abcdefghij\0", 24),
          "paths past dropBytes left out whole");
    program->requests[1].index1 = drop->drop;
    CHECK(mwinTestDrop(context, program->windows[0], (mwinPosition){0}, "/x\0", 3, nullptr, 0) ==
                  mwin_success &&
              mwinDestroyWindow(context, program->windows[0]) == mwin_success,
          "a drop on a window that goes");
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        break;
    case 1:
    {
        mwinWindowId window = program->windows[0];
        static const char files[] = "/a/b.txt\0/\xC3\xA9.png\0bad\xFF\0";
        Drag(context, window, mwin_eventDragEntered, 10.0f, 20.0f);
        Drag(context, window, mwin_eventDragMoved, 30.0f, 40.0f);
        Drag(context, window, mwin_eventDragMoved, 50.0f, 60.0f);
        CHECK(mwinTestDrop(context, window, (mwinPosition){50.0f, 60.0f}, files, sizeof(files) - 1,
                           "hi\xC3(", 4) == mwin_success,
              "a drop");
        break;
    }
    case 2:
        CheckDrop(program, context);
        break;
    case 3:
        CheckLeft(program, context);
        break;
    case 4:
        CheckLimits(program, context);
        break;
    case 5:
        CheckBytes(program, context);
        break;
    default:
        CHECK(mwinGetDroppedFiles(context, program->requests[1].index1, nullptr, 0, &(size_t){0}) ==
                  mwin_errorCapacity,
              "no drop for a window that went");
        program->done = true;
        break;
    }
}

// Text of dropBytes exactly is whole, and the drop not truncated.
static void ExactStep(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    if (step == 1)
    {
        CHECK(mwinTestDrop(context, program->windows[0], (mwinPosition){0}, "/y\0", 3,
                           "0123456789abcdefghijklmnopqrstuv", BYTES) == mwin_success,
              "a drop of text of the limit exactly");
        return;
    }
    const mwinDropEvent* drop = &program->events[0].data.drop;
    CHECK(program->eventCount == 1 && drop->fileCount == 1 && drop->textLength == BYTES &&
              !drop->truncated &&
              Copied(context, drop->drop, true, "0123456789abcdefghijklmnopqrstuv", BYTES),
          "the text whole, nothing truncated");
    program->done = true;
}

int main(void)
{
    Program exact = {.step = ExactStep};
    mwinContextDef exactDef = mwinDefaultContextDef();
    exactDef.limits.dropBytes = BYTES;
    CHECK(RunWith(&exact, exactDef) == mwin_success && exact.done, "the exact program runs");
    Program program = {.step = Step};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.droppedFiles = FILES;
    def.limits.dropBytes = BYTES;
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
