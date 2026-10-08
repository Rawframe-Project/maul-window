// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's drag and drop in headless Chrome
// (test/web_runner.cjs), with drag events the test dispatches on the
// window's canvas, each carrying a DataTransfer: a drag's records with
// where it is and what it carries, a drag over that has not moved not
// reported again, a drop delivering its files' names and its text; a
// drag that leaves; a drag of neither files nor text left alone; and a
// drop at the limits: a name of the window's text bytes kept, one a
// byte longer left out, text of the drop bytes kept.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/drop.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>

#define DEADLINE_MS 10000.0
#define MAX_RECORDS 32
#define NAME_BYTES  16
#define DROP_BYTES  64

typedef enum Phase
{
    phaseCreate,
    phaseDrop,
    phaseLeave,
    phaseOther,
    phaseLimits,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    double startMs;
    mwinWindowId window;
    char selector[128];
    mwinEvent records[MAX_RECORDS];
    int count;
    int frames;
    bool timedOut;
} Program;

EM_JS_DEPS(test_web_drop, "$UTF8ToString");

// clang-format off
// Dispatches drag events on the canvas: each a type, where, and what
// the drag carries ("files", "text", "html"); whether the page's
// default was kept from the last.
EM_JS(bool, DragEvents, (const char* selector, int which), {
    const canvas = document.querySelector(UTF8ToString(selector));
    const box = canvas.getBoundingClientRect();
    const transfer = kinds => {
        const data = new DataTransfer();
        if (kinds.includes('files')) {
            data.items.add(new File(['x'], 'a.txt'));
            data.items.add(new File(['y'], 'é.png'));
        }
        if (kinds.includes('text')) {
            data.setData('text/plain', 'hi ☃');
        }
        if (kinds.includes('html')) {
            data.setData('text/html', '<b>hi</b>');
        }
        if (kinds.includes('limits')) {
            data.items.add(new File(['x'], 'a'.repeat(12) + '.txt'));
            data.items.add(new File(['y'], 'b'.repeat(13) + '.txt'));
            data.setData('text/plain', 'c'.repeat(64));
        }
        return data;
    };
    const sequences = [
        [['dragenter', 10, 20], ['dragover', 30, 40], ['dragover', 30, 40], ['drop', 50, 60]],
        [['dragenter', 5, 5], ['dragleave', 5, 5]],
        [['dragenter', 5, 5], ['drop', 5, 5]],
        [['dragenter', 5, 5], ['drop', 5, 5]],
    ];
    const kinds = [['files', 'text'], ['files', 'text'], ['html'], ['limits']][which];
    let kept = false;
    sequences[which].forEach(([type, x, y]) => {
        const event = new DragEvent(type, {bubbles: true, cancelable: true,
                                           clientX: box.left + x, clientY: box.top + y,
                                           dataTransfer: transfer(kinds)});
        kept = !canvas.dispatchEvent(event);
    });
    return kept;
});
// clang-format on

static const mwinEvent* Find(const Program* program, mwinEventType type, int nth)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type && nth-- == 0)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

// The drag records among the drained ones: other records (the page's
// focus moving under a loaded machine) may come too.
static int DragRecords(const Program* program)
{
    int count = 0;
    for (int i = 0; i < program->count; i++)
    {
        mwinEventType type = program->records[i].type;
        count += type >= mwin_eventDragEntered && type <= mwin_eventDropped;
    }
    return count;
}

static bool DragAt(const mwinEvent* event, float x, float y)
{
    return event != nullptr && event->data.drag.position.x == x &&
           event->data.drag.position.y == y &&
           event->data.drag.contents == (mwin_dragFiles | mwin_dragText);
}

static void CheckDrop(const Program* program, mwinContext* context)
{
    const mwinEvent* dropped = Find(program, mwin_eventDropped, 0);
    CHECK(DragAt(Find(program, mwin_eventDragEntered, 0), 10.0f, 20.0f) &&
              DragAt(Find(program, mwin_eventDragMoved, 0), 30.0f, 40.0f) &&
              Find(program, mwin_eventDragMoved, 1) == nullptr && dropped != nullptr &&
              dropped->data.drop.position.x == 50.0f && dropped->data.drop.fileCount == 2 &&
              !dropped->data.drop.truncated,
          "the drag's records, a still drag over not repeated, the drop");
    char bytes[64];
    size_t length = 0;
    uint32_t drop = dropped != nullptr ? dropped->data.drop.drop : 0;
    CHECK(mwinGetDroppedFiles(context, drop, bytes, sizeof(bytes), &length) == mwin_success &&
              length == 13 && memcmp(bytes, "a.txt\0\xC3\xA9.png\0", 13) == 0,
          "the files' names, each ended by a NUL");
    CHECK(mwinGetDroppedText(context, drop, bytes, sizeof(bytes), &length) == mwin_success &&
              length == 6 && memcmp(bytes, "hi \xE2\x98\x83", 6) == 0,
          "the text");
}

// At the limits: the name of 16 bytes kept, the one of 17 left out and
// the drop marked truncated, the text of 64 bytes whole.
static void CheckLimits(const Program* program, mwinContext* context)
{
    const mwinEvent* dropped = Find(program, mwin_eventDropped, 0);
    CHECK(dropped != nullptr && dropped->data.drop.fileCount == 1 && dropped->data.drop.truncated,
          "a name past the window's text bytes left out");
    char bytes[DROP_BYTES + 1];
    size_t length = 0;
    uint32_t drop = dropped != nullptr ? dropped->data.drop.drop : 0;
    CHECK(mwinGetDroppedFiles(context, drop, bytes, sizeof(bytes), &length) == mwin_success &&
              length == NAME_BYTES + 1 && memcmp(bytes, "aaaaaaaaaaaa.txt", NAME_BYTES + 1) == 0,
          "a name of the window's text bytes kept");
    CHECK(mwinGetDroppedText(context, drop, bytes, sizeof(bytes), &length) == mwin_success &&
              length == DROP_BYTES && bytes[0] == 'c' && bytes[DROP_BYTES - 1] == 'c',
          "text of the drop bytes kept");
}

static bool Ready(Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    case phaseDrop:
        return Find(program, mwin_eventDropped, 0) != nullptr;
    case phaseLeave:
        return Find(program, mwin_eventDragLeft, 0) != nullptr;
    case phaseLimits:
        return Find(program, mwin_eventDropped, 0) != nullptr;
    default:
        // Nothing comes: a few frames show it.
        return ++program->frames > 10;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseCreate:
    {
        mwinNativeHandles handles;
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success &&
                  handles.handles.web.selectorLength < sizeof(program->selector),
              "the canvas");
        memcpy(program->selector, handles.handles.web.selector, handles.handles.web.selectorLength);
        CHECK(DragEvents(program->selector, 0), "a drop's default kept from the page");
        break;
    }
    case phaseDrop:
        CheckDrop(program, context);
        (void)DragEvents(program->selector, 1);
        break;
    case phaseLeave:
        CHECK(DragAt(Find(program, mwin_eventDragEntered, 0), 5.0f, 5.0f) &&
                  Find(program, mwin_eventDropped, 0) == nullptr,
              "a drag that leaves");
        CHECK(!DragEvents(program->selector, 2), "a drag of neither left to the page");
        break;
    case phaseOther:
        CHECK(DragRecords(program) == 0, "and not reported");
        (void)DragEvents(program->selector, 3);
        break;
    default:
        CheckLimits(program, context);
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startMs = mwinWebNow();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    program->startMs = mwinWebNow();
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
    if (program->phase == phaseDone)
    {
        return mwin_frameStop;
    }
    if (mwinWebNow() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    const Program* program = user;
    CHECK(status == mwin_success && !program->timedOut && program->phase == phaseDone,
          "every phase ran in time");
    (void)printf("mwin-test: exit %d\n", s_failures == 0 ? 0 : 1);
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    def.context.limits.textBytesPerWindow = NAME_BYTES;
    def.context.limits.dropBytes = DROP_BYTES;
    // With Emscripten mwinRun returns only when init failed; without it,
    // it returns once init succeeded and the page's frames run the program
    // on (mwin-0022). Either way quit reports.
    if (mwinRun(&def) == mwin_success)
    {
        return 0;
    }
    (void)printf("mwin-test: exit 1\n");
    return 1;
}
