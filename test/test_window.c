// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The window contract against the test backend: creation and its
// notifications, requests paired with exactly one completion after the
// notifications they caused, superseding, the answers a platform may
// give, close and destroy, stale ids, the named limits, one ordered
// stream across windows, coalescing, timestamps, refusals, and every
// byte of memory returned. Each test is a program: its frame function
// runs one step per frame, and the test backend pumps between frames.

#include "test_program.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void CreationStep(Program* program, mwinContext* context, int step)
{
    mwinWindowState state;
    if (step == 0)
    {
        CHECK(mwinTestSetScale(context, 1.5f) == mwin_success, "scale");
        program->windows[0] = Create(context, &program->requests[0]);
        CHECK(program->windows[0].index1 != 0, "the id is live at once");
        CHECK(mwinGetWindowState(context, program->windows[0], &state) == mwin_success &&
                  !state.created,
              "not created before the platform answers");
        return;
    }
    Drain(program, context);
    static const mwinEventType expected[] = {
        mwin_eventWindowCreated,    mwin_eventScaleChanged, mwin_eventResized,
        mwin_eventPixelSizeChanged, mwin_eventModeChanged,  mwin_eventShown,
        mwin_eventRequestCompleted,
    };
    CHECK(Types(program, expected, 7), "creation reports, then its completion");
    const mwinCompletion* completion = &program->events[6].data.completion;
    CHECK(SameId(completion->request, program->requests[0]) &&
              completion->kind == mwin_requestCreate && completion->outcome == mwin_outcomeDone,
          "the completion names the creation request");
    CHECK(SameWindow(program->events[0].window, program->windows[0]), "records name the window");
    CHECK(mwinGetWindowState(context, program->windows[0], &state) == mwin_success &&
              state.created && state.visible && state.scale == 1.5f && state.size.width == 640.0f &&
              state.pixelSize.width == 960 && state.pixelSize.height == 720,
          "the state follows the notifications");
    program->done = true;
}

static void TestCreation(void)
{
    Program program = {.step = CreationStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
    CHECK(program.quitCalls == 1 && program.quitStatus == mwin_success, "quit once");
}

static void RequestStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        CHECK(mwinRequestSize(context, window, (mwinSize){800.0f, 600.0f}, &program->requests[0]) ==
                  mwin_success,
              "a size request");
        CHECK(mwinRequestTitle(context, window, "Neu", 3, &program->requests[1]) == mwin_success,
              "a title request");
        return;
    }
    static const mwinEventType expected[] = {mwin_eventResized, mwin_eventPixelSizeChanged,
                                             mwin_eventRequestCompleted,
                                             mwin_eventRequestCompleted};
    CHECK(Types(program, expected, 4), "notifications, then completions in order");
    CHECK(SameId(program->events[2].data.completion.request, program->requests[0]) &&
              SameId(program->events[3].data.completion.request, program->requests[1]),
          "each request answered once");
    CHECK(program->events[0].data.size.width == 800.0f, "the new size");
    char title[8];
    size_t length = 0;
    CHECK(mwinTestGetTitle(context, window, title, sizeof(title), &length) == mwin_success &&
              length == 3 && memcmp(title, "Neu", 3) == 0,
          "the platform shows the new title");
    program->done = true;
}

static void TestRequests(void)
{
    Program program = {.step = RequestStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void SupersedeStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        CHECK(mwinTestHold(context, true) == mwin_success, "hold");
        CHECK(mwinRequestSize(context, window, (mwinSize){100.0f, 100.0f}, &program->requests[0]) ==
                      mwin_success &&
                  mwinRequestSize(context, window, (mwinSize){200.0f, 100.0f},
                                  &program->requests[1]) == mwin_success,
              "two size requests");
        return;
    }
    if (step == 2)
    {
        CHECK(program->eventCount == 1 &&
                  program->events[0].data.completion.outcome == mwin_outcomeSuperseded &&
                  SameId(program->events[0].data.completion.request, program->requests[0]),
              "the first is superseded at once");
        CHECK(mwinTestHold(context, false) == mwin_success, "release");
        return;
    }
    static const mwinEventType expected[] = {mwin_eventResized, mwin_eventPixelSizeChanged,
                                             mwin_eventRequestCompleted};
    CHECK(Types(program, expected, 3) && program->events[0].data.size.width == 200.0f &&
              SameId(program->events[2].data.completion.request, program->requests[1]),
          "the second is carried out");
    program->done = true;
}

static void TestSuperseding(void)
{
    Program program = {.step = SupersedeStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void AnswerStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        CHECK(mwinTestSetAnswer(context, mwin_requestPosition, mwin_outcomeUnsupported) ==
                      mwin_success &&
                  mwinTestSetAnswer(context, mwin_requestFocus, mwin_outcomeDenied) == mwin_success,
              "answers");
        CHECK(mwinTestSetAnswer(context, mwin_requestFocus, mwin_outcomeSuperseded) ==
                  mwin_errorInvalid,
              "only the core supersedes");
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        CHECK(mwinRequestPosition(context, window, (mwinPosition){10.0f, 20.0f}, nullptr) ==
                      mwin_success &&
                  mwinRequestFocus(context, window, nullptr) == mwin_success,
              "requests the platform refuses");
        return;
    }
    CHECK(program->eventCount == 2 &&
              program->events[0].data.completion.outcome == mwin_outcomeUnsupported &&
              program->events[1].data.completion.outcome == mwin_outcomeDenied,
          "refusals are completions, with nothing moved");
    program->done = true;
}

static void TestAnswers(void)
{
    Program program = {.step = AnswerStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void CloseStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId window = program->windows[0];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        mwinEvent close = {.type = mwin_eventCloseRequested, .window = window};
        CHECK(mwinTestPost(context, &close) == mwin_success, "the close button");
        return;
    }
    if (step == 2)
    {
        mwinWindowState state;
        CHECK(program->eventCount == 1 && program->events[0].type == mwin_eventCloseRequested &&
                  mwinGetWindowState(context, window, &state) == mwin_success,
              "a close request closes nothing");
        CHECK(mwinTestHold(context, true) == mwin_success, "hold");
        CHECK(mwinRequestMode(context, window, mwin_modeMaximized, &program->requests[0]) ==
                  mwin_success,
              "a request in flight");
        CHECK(mwinDestroyWindow(context, window) == mwin_success, "destroy");
        CHECK(mwinDestroyWindow(context, window) == mwin_errorStale, "destroy once");
        mwinWindowState state2;
        CHECK(mwinGetWindowState(context, window, &state2) == mwin_errorStale &&
                  mwinRequestFocus(context, window, nullptr) == mwin_errorStale &&
                  mwinRequestSize(context, window, (mwinSize){1.0f, 1.0f}, nullptr) ==
                      mwin_errorStale &&
                  mwinRequestTitle(context, window, "x", 1, nullptr) == mwin_errorStale,
              "a stale id is refused");
        Drain(program, context);
        static const mwinEventType expected[] = {mwin_eventRequestCompleted,
                                                 mwin_eventWindowDestroyed};
        CHECK(Types(program, expected, 2) &&
                  program->events[0].data.completion.outcome == mwin_outcomeCancelled,
              "its request is cancelled, then it is destroyed");
        CHECK(mwinTestHold(context, false) == mwin_success, "release");
        return;
    }
    CHECK(program->eventCount == 0, "nothing answers a destroyed window's requests");
    program->done = true;
}

static void TestCloseAndDestroy(void)
{
    Program program = {.step = CloseStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void LimitStep(Program* program, mwinContext* context, int step)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    mwinWindowId extra = {0};
    if (step == 0)
    {
        for (int i = 0; i < 8; i++)
        {
            CHECK(mwinCreateWindow(context, &def, &program->windows[i], nullptr) == mwin_success,
                  "eight windows");
        }
        CHECK(mwinCreateWindow(context, &def, &extra, nullptr) == mwin_errorCapacity, "not nine");
        CHECK(mwinDestroyWindow(context, program->windows[0]) == mwin_success, "destroy one");
        CHECK(mwinCreateWindow(context, &def, &extra, nullptr) == mwin_errorCapacity,
              "its slot waits for its destroyed record");
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        CHECK(mwinCreateWindow(context, &def, &program->windows[0], nullptr) == mwin_success,
              "the slot is free once drained");
        mwinWindowId window = program->windows[1];
        CHECK(mwinTestHold(context, true) == mwin_success, "hold");
        for (int i = 0; i < 32; i++)
        {
            CHECK(mwinRequestSize(context, window, (mwinSize){10.0f + (float)i, 10.0f}, nullptr) ==
                      mwin_success,
                  "32 requests");
        }
        CHECK(mwinRequestSize(context, window, (mwinSize){5.0f, 5.0f}, nullptr) ==
                  mwin_errorCapacity,
              "33 are too many while the completions wait");
        return;
    }
    CHECK(mwinRequestSize(context, program->windows[1], (mwinSize){5.0f, 5.0f}, nullptr) ==
              mwin_success,
          "draining frees them");
    program->done = true;
}

static void TestLimits(void)
{
    Program program = {.step = LimitStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void OrderStep(Program* program, mwinContext* context, int step)
{
    mwinWindowId a = program->windows[0];
    mwinWindowId b = program->windows[1];
    if (step == 0)
    {
        program->windows[0] = Create(context, nullptr);
        program->windows[1] = Create(context, nullptr);
        return;
    }
    Drain(program, context);
    if (step == 1)
    {
        mwinEvent event = {.type = mwin_eventFocusLost, .window = a};
        CHECK(mwinTestSetTime(context, 1000) == mwin_success, "time");
        CHECK(mwinTestPost(context, &event) == mwin_success, "focus leaves A");
        event = (mwinEvent){.type = mwin_eventFocusGained, .window = b};
        CHECK(mwinTestPost(context, &event) == mwin_success, "and enters B");
        for (int i = 1; i <= 3; i++)
        {
            event = (mwinEvent){.type = mwin_eventResized, .window = a};
            event.data.size = (mwinSize){(float)(100 * i), 50.0f};
            CHECK(mwinTestPost(context, &event) == mwin_success, "the user drags A's edge");
        }
        CHECK(mwinTestSetTime(context, 999) == mwin_errorInvalid, "time goes forward");
        return;
    }
    static const mwinEventType expected[] = {mwin_eventFocusLost, mwin_eventInputStateReset,
                                             mwin_eventFocusGained, mwin_eventResized};
    CHECK(Types(program, expected, 4) && SameWindow(program->events[0].window, a) &&
              SameWindow(program->events[1].window, a) && SameWindow(program->events[2].window, b),
          "one stream in arrival order across windows, a reset after focus leaves");
    CHECK(program->events[3].data.size.width == 300.0f, "resizes coalesce to the newest");
    CHECK(program->events[0].timeNs == 1000, "records carry the platform's time");
    program->done = true;
}

static void TestOrderAndCoalescing(void)
{
    Program program = {.step = OrderStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void RefusalStep(Program* program, mwinContext* context, int step)
{
    (void)step;
    CHECK(mwinGetContextMisuse(context) == 0 && mwinGetContextMisuse(nullptr) == 0,
          "no misuse yet");
    mwinWindowDef def = mwinDefaultWindowDef();
    mwinWindowId window = {0};
    def.cookie = 0;
    CHECK(mwinCreateWindow(context, &def, &window, nullptr) == mwin_errorInvalid, "cookie");
    def = mwinDefaultWindowDef();
    def.size.width = NAN;
    CHECK(mwinCreateWindow(context, &def, &window, nullptr) == mwin_errorInvalid, "size");
    def = mwinDefaultWindowDef();
    def.title = "\xC0\xAF";
    def.titleLength = 2;
    CHECK(mwinCreateWindow(context, &def, &window, nullptr) == mwin_errorInvalid,
          "an overlong title is not UTF-8");
    def = mwinDefaultWindowDef();
    def.canvasLength = 1;
    CHECK(mwinCreateWindow(context, &def, &window, nullptr) == mwin_errorInvalid,
          "a canvas selector with no text");
    static char longSelector[MWIN_CANVAS_SELECTOR_BYTES + 1];
    memset(longSelector, 'a', sizeof(longSelector));
    def.canvas = longSelector;
    def.canvasLength = sizeof(longSelector);
    CHECK(mwinCreateWindow(context, &def, &window, nullptr) == mwin_errorInvalid,
          "a canvas selector past its limit");
    window = Create(context, nullptr);
    static char longTitle[1025];
    memset(longTitle, 'a', sizeof(longTitle));
    CHECK(mwinRequestTitle(context, window, longTitle, sizeof(longTitle), nullptr) ==
              mwin_errorCapacity,
          "a title past the limit");
    CHECK(mwinRequestTitle(context, window, "\xFF", 1, nullptr) == mwin_errorInvalid,
          "a title that is not UTF-8");
    CHECK(mwinRequestMode(context, window, 9, nullptr) == mwin_errorInvalid, "a mode");
    CHECK(mwinRequestPosition(context, window, (mwinPosition){INFINITY, 0.0f}, nullptr) ==
              mwin_errorInvalid,
          "a position");
    CHECK(mwinRequestSize(context, window, (mwinSize){-1.0f, 1.0f}, nullptr) == mwin_errorInvalid,
          "a negative size");
    mwinEvent event = {.type = mwin_eventRequestCompleted, .window = window};
    CHECK(mwinTestPost(context, &event) == mwin_errorInvalid, "completions are the core's");
    CHECK(mwinNextEvent(nullptr, &event) == mwin_errorInvalid, "a NULL context");
    CHECK(mwinGetWindowState(context, window, nullptr) == mwin_errorInvalid,
          "a getter's missing output");
    mwinWindowId never = {3, 7};
    CHECK(mwinDestroyWindow(context, never) == mwin_errorStale, "an id never given");
    // Each invalid refusal of the live context counts; the capacity
    // refusal, the NULL context and the stale id do not.
    CHECK(mwinGetContextMisuse(context) == 11, "every misuse counted");
    program->done = true;
}

static void TestRefusals(void)
{
    Program program = {.step = RefusalStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

static void NeverStep(Program* program, mwinContext* context, int step)
{
    (void)program;
    (void)context;
    (void)step;
    CHECK(false, "no frame after init fails");
}

typedef struct Counter
{
    size_t live;
    size_t calls;
    size_t failAfter;
} Counter;

static void* CountAlloc(size_t size, size_t alignment, void* context)
{
    Counter* counter = context;
    (void)alignment;
    if (counter->calls++ >= counter->failAfter)
    {
        return nullptr;
    }
    counter->live += size;
    return malloc(size);
}

static void CountFree(void* memory, size_t size, size_t alignment, void* context)
{
    Counter* counter = context;
    (void)alignment;
    counter->live -= size;
    free(memory);
}

static void TestRunAndMemory(void)
{
    Program program = {.step = NeverStep, .initStatus = mwin_errorPlatform};
    CHECK(Run(&program) == mwin_errorPlatform && program.quitCalls == 1 &&
              program.quitStatus == mwin_errorPlatform,
          "a failed init skips the frames and goes to quit");
    Counter counter = {.failAfter = 100};
    mwinContextDef def = mwinDefaultContextDef();
    def.allocator = (mwinAllocator){CountAlloc, CountFree, &counter};
    program = (Program){.step = CreationStep};
    CHECK(RunWith(&program, def) == mwin_success && counter.calls > 0 && counter.live == 0,
          "every byte returned");
    counter = (Counter){.failAfter = 0};
    CHECK(RunWith(&program, def) == mwin_errorCapacity, "no memory, no context");
    def = mwinDefaultContextDef();
    def.limits.notificationsPerWindow = 20;
    CHECK(RunWith(&program, def) == mwin_errorInvalid, "limits below the bound");
    mwinAppDef app = mwinDefaultAppDef();
    CHECK(mwinRun(&app) == mwin_errorInvalid, "an app without functions");
    app.init = Init;
    app.frame = Frame;
    app.user = &program;
    // A native backend runs init, which refuses here; without one, or
    // without a window system to reach, the run is refused.
    program = (Program){.step = CreationStep, .initStatus = mwin_errorState};
    mwinResult native = mwinRun(&app);
    CHECK(native == mwin_errorState || native == mwin_errorUnsupported ||
              native == mwin_errorPlatform,
          "the native backend, or none");
}

int main(void)
{
    TestCreation();
    TestRequests();
    TestSuperseding();
    TestAnswers();
    TestCloseAndDestroy();
    TestLimits();
    TestOrderAndCoalescing();
    TestRefusals();
    TestRunAndMemory();
    return s_failures == 0 ? 0 : 1;
}
