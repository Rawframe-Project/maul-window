// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's services in headless Chrome, against stand-ins for
// window.open and the wake lock: an address opened in a new tab cut
// from the page, a blocked popup denied, a file never revealed; the
// screen kept awake while the window shows, let go while it is hidden,
// asked for again after the page released it (before the lock came, and
// after), let go when the window no longer asks and at the page's end.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/services.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <string.h>

#define DEADLINE_MS 10000.0

typedef enum Phase
{
    phaseCreate,
    phaseAwake,
    phaseHidden,
    phaseShown,
    phaseRegained,
    phaseRegainedLater,
    phaseAsleep,
    phaseEnding,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    double startMs;
    mwinWindowId window;
    mwinRequestId request;
    int outcome;
    bool timedOut;
} Program;

// clang-format off
// window.open that records the address and returns a tab, or none as a
// blocker would.
EM_JS(void, StandInOpen, (bool blocked), {
    globalThis.mwinTab = {opener: window};
    window.open = (address, target) => {
        globalThis.mwinOpened = address + ' ' + target;
        return blocked ? null : globalThis.mwinTab;
    };
});

EM_JS(bool, OpenedCut, (void), {
    return globalThis.mwinOpened === 'https://example.com/é _blank' &&
           globalThis.mwinTab.opener === null;
});

// A wake lock that counts what it was asked and the locks it holds;
// Drop releases them as the page would when hidden.
EM_JS(void, StandInWakeLock, (void), {
    class Lock extends EventTarget {
        release() {
            if (!this.released) {
                this.released = true;
                wake.active--;
                this.dispatchEvent(new Event('release'));
            }
            return Promise.resolve();
        }
    }
    const wake = {asked: 0, active: 0, locks: []};
    wake.request = type => {
        wake.asked++;
        wake.active++;
        const lock = new Lock();
        wake.locks.push(lock);
        return type === 'screen' ? Promise.resolve(lock) : Promise.reject(new TypeError());
    };
    Object.defineProperty(navigator, 'wakeLock', {value: wake, configurable: true});
    globalThis.mwinWake = wake;
});

EM_JS(bool, Holds, (int asked, int active), {
    return globalThis.mwinWake.asked === asked && globalThis.mwinWake.active === active;
});

// Releases the locks as the page does when hidden, and says it shows:
// at once, before the lock asked for last has reached the backend, or
// once it has.
EM_JS(void, DropLocks, (bool later), {
    const drop = () => {
        globalThis.mwinWake.locks.forEach(lock => lock.release());
        document.dispatchEvent(new Event('visibilitychange'));
    };
    if (later) {
        Promise.resolve().then(drop);
    } else {
        drop();
    }
});

// Ends the test once the page is let go, after the program's end.
EM_JS(void, ExitAfterEnd, (int failures), {
    setTimeout(() => {
        const released = globalThis.mwinWake.active === 0;
        if (!released) {
            console.log('FAIL: the screen let go at the page\'s end');
        }
        console.log('mwin-test: exit ' + (failures === 0 && released ? 0 : 1));
    }, 0);
});
// clang-format on

static const char s_address[] = "https://example.com/\xC3\xA9";

// The completions drained; the outcome of the one followed kept.
static void Drain(mwinContext* context, Program* program)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == program->request.index1 &&
            completion->request.generation == program->request.generation)
        {
            program->outcome = completion->outcome;
        }
    }
}

// Asks for a request whose outcome comes at once.
static int Now(mwinContext* context, Program* program, mwinResult submitted)
{
    program->outcome = -1;
    if (submitted != mwin_success)
    {
        return -1;
    }
    Drain(context, program);
    return program->outcome;
}

static void CheckOpen(mwinContext* context, Program* program)
{
    mwinWindowId window = program->window;
    StandInOpen(false);
    CHECK(Now(context, program,
              mwinRequestOpenUrl(context, window, s_address, sizeof(s_address) - 1,
                                 &program->request)) == mwin_outcomeDone &&
              OpenedCut(),
          "an address opened in a new tab cut from the page");
    StandInOpen(true);
    CHECK(Now(context, program,
              mwinRequestOpenUrl(context, window, s_address, sizeof(s_address) - 1,
                                 &program->request)) == mwin_outcomeDenied,
          "a blocked popup denied");
    CHECK(
        Now(context, program, mwinRequestRevealFile(context, window, "/a", 2, &program->request)) ==
            mwin_outcomeUnsupported,
        "a file never revealed");
    StandInWakeLock();
    CHECK(Now(context, program, mwinRequestKeepAwake(context, window, true, &program->request)) ==
              mwin_outcomeDone,
          "keep the screen awake");
}

// Whether the phase's wait is over.
static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return program->outcome == mwin_outcomeDone;
    case phaseAwake:
    case phaseEnding:
        return Holds(program->phase == phaseAwake ? 1 : 5, 1);
    case phaseHidden:
        return Holds(1, 0);
    case phaseShown:
        return Holds(2, 1);
    case phaseRegained:
        return Holds(3, 1);
    case phaseRegainedLater:
        return Holds(4, 1);
    case phaseAsleep:
        return Holds(4, 0);
    default:
        return false;
    }
}

static void Advance(mwinContext* context, Program* program)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case phaseCreate:
        CheckOpen(context, program);
        break;
    case phaseAwake:
        CHECK(mwinRequestVisible(context, window, false, nullptr) == mwin_success, "hide");
        break;
    case phaseHidden:
        CHECK(mwinRequestVisible(context, window, true, nullptr) == mwin_success, "show");
        break;
    case phaseShown:
        DropLocks(false);
        break;
    case phaseRegained:
        DropLocks(true);
        break;
    case phaseRegainedLater:
        CHECK(mwinRequestKeepAwake(context, window, false, nullptr) == mwin_success,
              "let the screen go");
        break;
    case phaseAsleep:
        CHECK(mwinRequestKeepAwake(context, window, true, nullptr) == mwin_success,
              "keep the screen awake to the end");
        break;
    default:
        break;
    }
    program->phase++;
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = emscripten_get_now();
    program->outcome = -1;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Drain(context, program);
    if (Ready(program))
    {
        Advance(context, program);
    }
    if (program->phase == phaseDone)
    {
        return mwin_frameStop;
    }
    if (emscripten_get_now() - program->startMs > DEADLINE_MS)
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
    ExitAfterEnd(s_failures);
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    (void)printf("mwin-test: exit 1\n");
    return 1;
}
