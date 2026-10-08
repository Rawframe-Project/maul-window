// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's lifecycle in headless Chrome (test/web_runner.cjs):
// a hidden page suspends and occludes, a frame running inside the
// browser's event; shown again it resumes; pagehide and pageshow from
// the back-forward cache do the same; a canvas taken out of the
// document loses its surface and has it again when it is back; and a
// frame that stops while the page goes away ends the program there, the
// page letting go of the window's canvas and its host as it ends.
// The page's hiding is played by the test, between frames.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>

#define DEADLINE_MS 10000.0
#define MAX_RECORDS 64

typedef enum Phase
{
    phaseCreate,
    phaseHide,
    phaseShow,
    phaseLeave,
    phaseReturn,
    phaseDetach,
    phaseAttach,
    phaseStop,
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
    // A Suspending or Resuming record was read inside the page's event.
    bool critical;
    bool timedOut;
} Program;

EM_JS_DEPS(test_web_lifecycle, "$UTF8ToString");

// clang-format off
// Plays the page hiding or showing, after this frame, and marks the
// browser's event while it runs.
EM_JS(void, SetHidden, (bool hidden), {
    setTimeout(() => {
        Object.defineProperty(document, 'hidden', {value: !!hidden, configurable: true});
        Object.defineProperty(document, 'visibilityState',
                              {value: hidden ? 'hidden' : 'visible', configurable: true});
        globalThis.mwinInEvent = true;
        document.dispatchEvent(new Event('visibilitychange'));
        globalThis.mwinInEvent = false;
    });
});

// Plays leaving the page into the back-forward cache, or coming back.
EM_JS(void, Transition, (bool leave), {
    setTimeout(() => {
        globalThis.mwinInEvent = true;
        window.dispatchEvent(new PageTransitionEvent(leave ? 'pagehide' : 'pageshow',
                                                     {persisted: true}));
        globalThis.mwinInEvent = false;
    });
});

EM_JS(bool, InEvent, (void), {
    return globalThis.mwinInEvent === true;
});

// Takes the canvas out of the document, or puts it back.
EM_JS(void, Detach, (const char* selector, bool detach), {
    globalThis.mwinCanvas = globalThis.mwinCanvas || document.querySelector(UTF8ToString(selector));
    detach ? globalThis.mwinCanvas.remove() : document.body.appendChild(globalThis.mwinCanvas);
});

// Reports the status once the program's end is over, a failure where
// the page kept the canvas or its accessibility host.
EM_JS(void, ExitOnceEnded, (const char* selector, int status), {
    const canvas = UTF8ToString(selector);
    setTimeout(() => {
        const kept = document.querySelector(canvas) || document.querySelector(canvas + '-accessibility');
        if (kept) {
            console.log('FAIL: the page kept the window\'s canvas or its host');
        }
        console.log('mwin-test: exit ' + (kept ? 1 : status));
    }, 0);
});
// clang-format on

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        if (event.type == mwin_eventSuspending || event.type == mwin_eventResuming)
        {
            program->critical = InEvent();
        }
        program->records[program->count++] = event;
    }
}

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

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    case phaseHide:
    case phaseLeave:
        return Find(program, mwin_eventSuspended, 0) != nullptr;
    case phaseShow:
    case phaseReturn:
        return Find(program, mwin_eventResumed, 0) != nullptr;
    case phaseDetach:
        return Find(program, mwin_eventSurfaceLost, 0) != nullptr;
    default:
        return Find(program, mwin_eventSurfaceRestored, 0) != nullptr;
    }
}

// Whether a record came before another, both there.
static bool Before(const Program* program, mwinEventType first, mwinEventType second)
{
    const mwinEvent* a = Find(program, first, 0);
    const mwinEvent* b = Find(program, second, 0);
    return a != nullptr && b != nullptr && a < b;
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
        program->selector[handles.handles.web.selectorLength] = '\0';
        SetHidden(true);
        break;
    }
    case phaseHide:
        CHECK(program->critical && Before(program, mwin_eventSuspending, mwin_eventSuspended) &&
                  Find(program, mwin_eventOccluded, 0) != nullptr,
              "hidden: suspending in a frame inside the event, suspended, occluded");
        SetHidden(false);
        break;
    case phaseShow:
        CHECK(program->critical && Before(program, mwin_eventResuming, mwin_eventResumed) &&
                  Find(program, mwin_eventRevealed, 0) != nullptr,
              "shown: resuming in a frame inside the event, resumed, revealed");
        Transition(true);
        break;
    case phaseLeave:
        CHECK(program->critical && Before(program, mwin_eventSuspending, mwin_eventSuspended),
              "left for the back-forward cache: suspended");
        Transition(false);
        break;
    case phaseReturn:
        CHECK(program->critical && Before(program, mwin_eventResuming, mwin_eventResumed),
              "back from the cache: resumed");
        Detach(program->selector, true);
        break;
    case phaseDetach:
    {
        mwinWindowState state;
        CHECK(mwinGetWindowState(context, program->window, &state) == mwin_success &&
                  state.surfaceLost,
              "out of the document: the surface is lost");
        Detach(program->selector, false);
        break;
    }
    default:
        CHECK(Find(program, mwin_eventSurfaceRestored, 0) != nullptr, "back: the surface again");
        SetHidden(true);
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->critical = false;
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
    Collect(program, context);
    if (program->phase == phaseStop)
    {
        // The page goes away: the program ends in the event.
        if (Find(program, mwin_eventSuspending, 0) != nullptr && program->critical)
        {
            program->phase = phaseDone;
            return mwin_frameStop;
        }
    }
    else if (Ready(program))
    {
        Advance(program, context);
    }
    if (mwinWebNow() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        for (int i = 0; i < program->count; i++)
        {
            (void)printf("  record %d\n", (int)program->records[i].type);
        }
        program->timedOut = true;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    const Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(!program->timedOut && program->phase == phaseDone, "every phase ran in time");
    CHECK(InEvent(), "the program ended inside the page's event");
    // The page lets go of the window once quit has returned.
    ExitOnceEnded(program->selector, s_failures == 0 ? 0 : 1);
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
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
