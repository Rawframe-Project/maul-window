// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend against records another script on the page pushes into
// its queue, in headless Chrome (test/web_runner.cjs): slots no window
// has, numbers that are no numbers or do not fit, codes past their
// tables, a pressure past 1, a tilt past upright, a canvas of negative
// size and pixels past 2^32, scales below and beyond any, a key past the
// page's table and one meaning a surrogate, a clipboard answer with an
// outcome there is not, and a time past what nanoseconds hold: the shapes
// of the findings of fuzz_web_records. Before them, entries that are not
// records at all, and a drop, a read's text and a composition's string of
// other shapes, which the page's own functions must not throw on. The
// page's devicePixelRatio is redefined as NaN before the program starts,
// and its power source set to 7: the screen's and the window's scales are
// above 0, and the power source a tristate. Each is left out or repaired:
// what the program reads is within the contract, and a key pushed after them arrives as sent.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/monitor.h"
#include "maul-window/system.h"
#include "maul-window/window.h"

#include <math.h>

#define DEADLINE_MS 10000.0

typedef struct Program
{
    mwinWindowId window;
    mwinRequestId read;
    double startMs;
    int frames;
    bool pushed;
    bool completed;
    bool sentinel;
    int wrong;
    bool timedOut;
} Program;

// clang-format off
// Pushes a record as the page's queue holds it, with its own time.
EM_JS(void, Push, (const mwinContext* context, int kind, int slot, int code, int extra, double x,
                   double y, double z, double w, double v, double timeMs), {
    Module.mwinWeb.get(context).queue.push([kind, slot, code, extra, x, y, z, w, v, 0, timeMs]);
});
// Entries no listener of the page's makes: not arrays, too short, of
// fields that are no numbers; a drop of names that are no strings and
// text that is no bytes, a read's text that is no bytes, and a
// composition's string that is no string, each followed by its record.
EM_JS(void, PushJunk, (const mwinContext* context, int slot), {
    const state = Module.mwinWeb.get(context);
    state.queue.push(null, 7, "record", {}, [3], [12, slot, 0, BigInt(0), "x", {}, null, 1, 1, 1, 10]);
    state.drops.push({names: [7, null, "a"], bytes: "text"});
    state.queue.push([27, slot, 0, 0, 1, 1, 0, 0, 0, 0, 10]);
    state.clipboardTexts.push("not bytes");
    state.strings.push({});
    state.queue.push([20, slot, 0, 0, 0, 0, 0, 0, 0, 0, 10]);
    state.onBattery = 7;
    state.queue.push([7, -1, 0, 0, 0, 0, 0, 0, 0, 0, 10]);
});

// What another script can do to the page before the program starts.
EM_JS(void, SpoilScale, (void), {
    Object.defineProperty(window, 'devicePixelRatio', {value: NaN, configurable: true});
});
// clang-format on

static void PushAll(const mwinContext* context, const Program* program)
{
    int slot = (int)program->window.index1 - 1;
    int generation = (int)program->window.generation;
    double huge = 1e30;
    double none = (double)NAN;
    double infinite = (double)INFINITY;
    // A mouse record past the page's kinds of mouse record, and a press of
    // a button past any, at no position.
    Push(context, 12, slot, 99, 0, 1, 1, 1, 0, 0, 10);
    Push(context, 12, slot, 3, 0, none, 1, 40, 0, 0, 10);
    // A drag past its phases, carrying no number of contents.
    Push(context, 26, slot, 31, 0, 1, 1, huge, 0, 0, 10);
    Push(context, 26, slot, 0, 0, 1, 1, huge, 0, 0, 10);
    // A pen pressing past 1, tilted past upright, with flags past its own.
    Push(context, 16, slot, 0, 0x7FFFFFFF, 1, 1, 5, huge, none, 10);
    // A touch whose pressure is no number.
    Push(context, 15, slot, 0, 1, 1, 1, none, 0, 0, 10);
    // A canvas of negative width and pixels past 2^32 and below none.
    Push(context, 1, slot, 0, 0, -5, 200, huge, -3, 0, 10);
    // Scales below any, of no number, and past every float's room.
    Push(context, 2, -1, 0, 0, -1, 0, 0, 0, 0, 10);
    Push(context, 2, -1, 0, 0, none, 0, 0, 0, 0, 10);
    // A key past the page's table of names, and one meaning a surrogate.
    Push(context, 10, slot, 999, 0, 1, 0, 0, 0, 0, 10);
    Push(context, 10, slot, 4, 0, 1, 0, 0xD800, 0, 0, 10);
    // A wheel turned without end, and motion of no number.
    Push(context, 13, slot, 0, 0, infinite, 0, 0, 0, 0, 10);
    Push(context, 14, slot, 0, 0, none, 1, 0, 0, 0, 10);
    // Records for slots no window has.
    Push(context, 3, 1000, 1, 0, 0, 0, 0, 0, 0, 10);
    Push(context, 12, -7, 0, 0, 1, 1, 0, 0, 0, 10);
    Push(context, 25, 1000, 0, 0, 0, 0, 0, 0, 0, 10);
    Push(context, 27, -3, 0, 0, 1, 1, 0, 0, 0, 10);
    // The clipboard read answered with an outcome there is not.
    Push(context, 25, slot, 99, generation, 0, 0, 0, 0, 0, 10);
    // A time past what nanoseconds hold.
    Push(context, 3, slot, 1, 0, 0, 0, 0, 0, 0, 1e306);
    // Then a key as the page sends it: "a" down.
    Push(context, 10, slot, mwin_codeKeyA, 0, 1, 0, 'a', 0, 0, 10);
}

static bool Finite(mwinPosition position)
{
    return isfinite(position.x) && isfinite(position.y);
}

static bool Unit(float value)
{
    return value >= 0.0f && value <= 1.0f;
}

// Whether a record is within the contract.
static bool Right(const mwinEvent* event)
{
    switch (event->type)
    {
    case mwin_eventKeyDown:
    case mwin_eventKeyUp:
        return event->data.key.code < 256 &&
               (event->data.key.key >= MWIN_KEY_NAMED ||
                (event->data.key.key <= 0x10FFFF &&
                 (event->data.key.key < 0xD800 || event->data.key.key > 0xDFFF)));
    case mwin_eventCursorMoved:
    case mwin_eventButtonDown:
    case mwin_eventButtonUp:
        return Finite(event->data.pointer.position) &&
               event->data.pointer.button <= mwin_buttonForward;
    case mwin_eventDragEntered:
    case mwin_eventDragMoved:
    case mwin_eventDragLeft:
        return Finite(event->data.drag.position) &&
               (event->data.drag.contents & ~(mwin_dragFiles | mwin_dragText)) == 0;
    case mwin_eventPenDown:
    case mwin_eventPenMoved:
    case mwin_eventPenUp:
    case mwin_eventPenButtonDown:
    case mwin_eventPenButtonUp:
        return Finite(event->data.pen.position) && Unit(event->data.pen.pressure) &&
               fabsf(event->data.pen.tiltX) <= 90.0f && fabsf(event->data.pen.tiltY) <= 90.0f;
    case mwin_eventTouchDown:
    case mwin_eventTouchMoved:
    case mwin_eventTouchUp:
    case mwin_eventTouchCancelled:
        return Finite(event->data.touch.position) &&
               (Unit(event->data.touch.pressure) || event->data.touch.pressure == -1.0f);
    case mwin_eventResized:
        return isfinite(event->data.size.width) && isfinite(event->data.size.height) &&
               event->data.size.width >= 0.0f && event->data.size.height >= 0.0f;
    case mwin_eventScaleChanged:
        return isfinite(event->data.scale.scale) && event->data.scale.scale > 0.0f;
    case mwin_eventWheel:
        return isfinite(event->data.wheel.x) && isfinite(event->data.wheel.y);
    case mwin_eventRawPointerDelta:
        return isfinite(event->data.delta.x) && isfinite(event->data.delta.y);
    case mwin_eventRequestCompleted:
        return event->data.completion.outcome <= mwin_outcomeTooLarge;
    default:
        return true;
    }
}

// Whether the facts and the screens the program reads are within the
// contract.
static bool RightFacts(const mwinContext* context, const Program* program)
{
    mwinSystemFacts facts;
    mwinMonitorId monitors[4];
    size_t count = 0;
    mwinWindowState state;
    bool right = mwinGetSystemFacts(context, &facts) == mwin_success &&
                 facts.onBattery <= mwin_yes && facts.theme <= mwin_themeDark &&
                 mwinGetMonitors(context, monitors, 4, &count) == mwin_success &&
                 mwinGetWindowState(context, program->window, &state) == mwin_success &&
                 isfinite(state.scale) && state.scale > 0.0f;
    for (size_t i = 0; right && i < count && i < 4; i++)
    {
        mwinMonitorInfo info;
        right = mwinGetMonitorInfo(context, monitors[i], &info) == mwin_success &&
                isfinite(info.scale) && info.scale > 0.0f;
    }
    return right;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    if (!RightFacts(context, program))
    {
        (void)printf("facts or scales out of the contract\n");
        program->wrong += 1;
    }
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (!Right(&event))
        {
            (void)printf("a record out of the contract: type %d\n", (int)event.type);
            program->wrong += 1;
        }
        program->completed = program->completed ||
                             (event.type == mwin_eventRequestCompleted &&
                              event.data.completion.request.index1 == program->read.index1 &&
                              event.data.completion.request.generation == program->read.generation);
        program->sentinel = program->sentinel ||
                            (event.type == mwin_eventKeyDown &&
                             event.data.key.code == mwin_codeKeyA && event.data.key.key == 'a');
    }
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
    Collect(program, context);
    program->frames += 1;
    if (!program->pushed && program->frames > 5)
    {
        CHECK(mwinRequestClipboardRead(context, program->window, &program->read) == mwin_success,
              "a clipboard read");
        PushJunk(context, (int)program->window.index1 - 1);
        PushAll(context, program);
        program->pushed = true;
    }
    if (program->pushed && program->sentinel && program->completed)
    {
        return mwin_frameStop;
    }
    if (mwinWebNow() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out: sentinel %d, read completed %d\n", program->sentinel,
                     program->completed);
        program->timedOut = true;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    const Program* program = user;
    CHECK(status == mwin_success && !program->timedOut, "the program runs on to the key sent last");
    CHECK(program->wrong == 0, "every record within the contract");
    (void)printf("mwin-test: exit %d\n", s_failures == 0 ? 0 : 1);
}

int main(void)
{
    static Program program;
    SpoilScale();
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    // With Emscripten mwinRun returns only when init failed; without it,
    // it returns once init succeeded and the page's frames run the program
    // on. Either way quit reports.
    if (mwinRun(&def) == mwin_success)
    {
        return 0;
    }
    (void)printf("mwin-test: exit 1\n");
    return 1;
}
