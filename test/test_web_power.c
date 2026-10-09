// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's power source in headless Chrome, from the Battery
// Status API, faked here before each context starts: an answer that comes
// after its context ended is not listened to, and a refusal is no error;
// unknown until the browser answers; on battery while the battery
// discharges, with mwin_eventPowerChanged; not on battery once it
// charges; low power never known.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/event.h"
#include "maul-window/system.h"

#define DEADLINE_MS 10000.0
#define MAX_RECORDS 32

typedef enum Phase
{
    phaseAsked,
    phaseDischarging,
    phaseCharging,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    double startMs;
    mwinEvent records[MAX_RECORDS];
    int count;
    int frames;
    bool timedOut;
} Program;

// clang-format off
// Batteries that discharge, one for each time the page asks, each given
// once Answer is called with its number, as a browser answers
// navigator.getBattery() later. Each counts the listeners it holds.
EM_JS(void, FakeBattery, (void), {
    Module.testBatteries = [];
    navigator.getBattery = () => {
        const battery = new EventTarget();
        battery.charging = false;
        battery.listeners = 0;
        const add = battery.addEventListener.bind(battery);
        const remove = battery.removeEventListener.bind(battery);
        battery.addEventListener = (type, handler) => { battery.listeners++; add(type, handler); };
        battery.removeEventListener = (type, handler) => { battery.listeners--; remove(type, handler); };
        let answer;
        let refuse;
        const answered = new Promise((resolve, reject) => {
            answer = () => resolve(battery);
            refuse = () => reject(new DOMException('not allowed', 'NotAllowedError'));
        });
        Module.testBatteries.push({battery, answer, refuse});
        return answered;
    };
});

EM_JS(void, Answer, (int which), {
    Module.testBatteries[which].answer();
});

// A refusal, as a page whose permissions policy forbids the battery
// gets.
EM_JS(void, RefuseBattery, (int which), {
    Module.testBatteries[which].refuse();
});

EM_JS(int, Listeners, (int which), {
    return Module.testBatteries[which].battery.listeners;
});

EM_JS(void, Plug, (int which), {
    const battery = Module.testBatteries[which].battery;
    battery.charging = true;
    battery.dispatchEvent(new Event('chargingchange'));
});
// clang-format on

static bool PowerChanged(const Program* program)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == mwin_eventPowerChanged)
        {
            return true;
        }
    }
    return false;
}

static bool PowerIs(const mwinContext* context, mwinTristate onBattery)
{
    mwinSystemFacts facts;
    return mwinGetSystemFacts(context, &facts) == mwin_success && facts.onBattery == onBattery &&
           facts.lowPower == mwin_unknown;
}

static bool Ready(Program* program)
{
    if (program->phase == phaseAsked)
    {
        // Nothing comes before the answer: a few frames show it.
        return ++program->frames > 10;
    }
    return PowerChanged(program);
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case phaseAsked:
        CHECK(!PowerChanged(program) && PowerIs(context, mwin_unknown),
              "unknown until the browser answers");
        CHECK(Listeners(0) == 0, "an answer after its context ended is not listened to");
        Answer(2);
        break;
    case phaseDischarging:
        CHECK(PowerIs(context, mwin_yes), "on battery while it discharges");
        Plug(2);
        break;
    default:
        CHECK(PowerIs(context, mwin_no), "not on battery once it charges");
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startMs = mwinWebNow();
}

// A program whose context ends before the browser answers.
static mwinResult Refuse(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_errorPlatform;
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    Program* program = user;
    program->startMs = mwinWebNow();
    return mwin_success;
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
    FakeBattery();
    mwinAppDef refused = mwinDefaultAppDef();
    refused.init = Refuse;
    refused.frame = Frame;
    bool ended = mwinRun(&refused) == mwin_errorPlatform;
    Answer(0);
    CHECK(ended, "a context ended before the browser answered");
    ended = mwinRun(&refused) == mwin_errorPlatform;
    RefuseBattery(1);
    CHECK(ended, "a context ended before the browser refused");
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
