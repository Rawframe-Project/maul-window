// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's drag and drop against the test compositor of
// wayland_server.h and its data device: a drag of files and text over
// the window accepted as text/uri-list with the copy action, entering,
// moving and dropping; the drop's paths from file URIs (a comment line
// left out, localhost and percent-escapes understood, a URI of another
// scheme left out and the drop marked truncated), its text repaired,
// and the drop finished; a drag of neither refused and not reported; a
// drag of text that leaves. Skipped (exit status 77) without
// XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_data_server.h"

#include "maul-window/drop.h"
#include "maul-window/event.h"

#include <stdio.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   50000000ull
#define MAX_RECORDS 32

typedef enum Phase
{
    phaseCreate,
    phaseDrop,
    phaseNeither,
    phaseLeave,
    phaseDone,
} Phase;

typedef struct Program
{
    DataServer* data;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinEvent records[MAX_RECORDS];
    int count;
    bool timedOut;
} Program;

static const char s_files[] = "file:///tmp/a%20b.txt\r\n# a comment\r\n"
                              "file://localhost/tmp/%C3%A9.png\r\nhttp://example.com/x\r\n";

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static const mwinEvent* Find(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

static bool DragAt(const mwinEvent* event, float x, float y, mwinDragContents contents)
{
    return event != nullptr && event->data.drag.position.x == x &&
           event->data.drag.position.y == y && event->data.drag.contents == contents;
}

static void CheckDrop(Program* program, mwinContext* context)
{
    const mwinEvent* dropped = Find(program, mwin_eventDropped);
    DataDrag drag = DataDragState(program->data);
    CHECK(DragAt(Find(program, mwin_eventDragEntered), 10.0f, 20.0f,
                 mwin_dragFiles | mwin_dragText) &&
              DragAt(Find(program, mwin_eventDragMoved), 30.0f, 40.0f,
                     mwin_dragFiles | mwin_dragText) &&
              Find(program, mwin_eventDragLeft) == nullptr,
          "a drag of files and text enters and moves");
    CHECK(strcmp(drag.accepted, "text/uri-list") == 0 &&
              drag.actions == WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY && drag.finished,
          "accepted as a uri-list, copied, and the drop finished");
    const mwinDropEvent* drop = &dropped->data.drop;
    char bytes[64];
    size_t length = 0;
    static const char paths[] = "/tmp/a b.txt\0/tmp/\xC3\xA9.png";
    CHECK(drop->position.x == 30.0f && drop->fileCount == 2 && drop->truncated &&
              mwinGetDroppedFiles(context, drop->drop, bytes, sizeof(bytes), &length) ==
                  mwin_success &&
              length == sizeof(paths) && memcmp(bytes, paths, length) == 0,
          "the paths of the file URIs, the other scheme left out");
    CHECK(mwinGetDroppedText(context, drop->drop, bytes, sizeof(bytes), &length) == mwin_success &&
              length == 6 && memcmp(bytes, "hi\xEF\xBF\xBD(", 6) == 0,
          "the text, repaired");
    DataDragEnter(program->data, 5.0, 5.0, nullptr, nullptr);
}

static bool Ready(Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventWindowCreated) != nullptr;
    case phaseDrop:
        return Find(program, mwin_eventDropped) != nullptr && DataDragState(program->data).finished;
    case phaseNeither:
        return NowNs() - program->startNs >= SETTLE_NS;
    default:
        // The client's accept reaches the compositor at its next pump.
        return Find(program, mwin_eventDragLeft) != nullptr &&
               DataDragState(program->data).accepts >= 1;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    DataServer* data = program->data;
    switch (program->phase)
    {
    case phaseCreate:
        DataDragEnter(data, 10.0, 20.0, s_files, "hi\xC3(");
        DataDragMotion(data, 30.0, 40.0);
        DataDragEnd(data, true);
        break;
    case phaseDrop:
        CheckDrop(program, context);
        break;
    case phaseNeither:
    {
        DataDrag drag = DataDragState(data);
        CHECK(drag.accepts >= 1 && drag.accepted[0] == '\0' && program->count == 0,
              "a drag of neither refused and not reported");
        DataDragEnd(data, false);
        DataDragEnter(data, 5.0, 5.0, nullptr, "x");
        DataDragEnd(data, false);
        break;
    }
    default:
        CHECK(DragAt(Find(program, mwin_eventDragEntered), 5.0f, 5.0f, mwin_dragText) &&
                  strcmp(DataDragState(data).accepted, "text/plain;charset=utf-8") == 0,
              "a drag of text that leaves");
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 500000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    static Server server;
    static DataServer data;
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&server, "us", ""))
    {
        return 77;
    }
    DataStart(&data, &server);
    Program program = {.data = &data};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    DataStop(&data);
    return s_failures == 0 ? 0 : 1;
}
