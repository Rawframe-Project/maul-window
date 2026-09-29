// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's clipboard in headless Chrome (test/web_runner.cjs
// grants the page the clipboard): text written, a NUL within it, and
// read back; text the page put there with a lone surrogate read with
// U+FFFD; text past the limit too large; a refusal denied, the last
// text kept; a read whose window went answered for no one, its text
// taken, so the next window's read has the clipboard's text now.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"

#include <string.h>

#define DEADLINE_MS 10000.0
#define LIMIT       32

typedef enum Phase
{
    phaseCreate,
    phaseWrite,
    phaseRead,
    phaseSurrogate,
    phaseTooLarge,
    phaseDenied,
    phaseGone,
    phaseWait,
    phaseCreateAgain,
    phaseAgain,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    double startMs;
    mwinWindowId window;
    mwinRequestId request;
    // Frames left to wait, and whether the phase's read was asked for
    // once the page's text was on the clipboard.
    int waitFrames;
    bool reading;
    bool timedOut;
} Program;

// clang-format off
// Puts text on the clipboard as another program would; Placed says when.
EM_JS(void, Place, (int which), {
    const texts = ['A\uD800B', 'x'.repeat(40), 'last'];
    globalThis.mwinPlaced = false;
    navigator.clipboard.writeText(texts[which]).then(() => globalThis.mwinPlaced = true);
});

EM_JS(bool, Placed, (void), {
    return globalThis.mwinPlaced === true;
});

// Refuses reads as a page without permission would, or stops refusing.
EM_JS(void, Refuse, (bool refuse), {
    if (refuse) {
        navigator.clipboard.readText =
            () => Promise.reject(new DOMException('refused', 'NotAllowedError'));
    } else {
        delete navigator.clipboard.readText;
    }
});
// clang-format on

static const char s_written[] = "h\xC3\xA9llo\0 \xF0\x9F\x98\x80";

static bool Found(mwinContext* context, const char* expected, size_t length)
{
    char text[LIMIT];
    size_t found = 0;
    return mwinGetClipboardText(context, text, sizeof(text), &found) == mwin_success &&
           found == length && memcmp(text, expected, length) == 0;
}

// The outcome of the phase's request once its completion is drained,
// or -1.
static int Outcome(Program* program, mwinContext* context)
{
    int outcome = -1;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == program->request.index1 &&
            completion->request.generation == program->request.generation)
        {
            outcome = completion->outcome;
        }
    }
    return outcome;
}

static void Read(Program* program, mwinContext* context)
{
    CHECK(mwinRequestClipboardRead(context, program->window, &program->request) == mwin_success,
          "a read");
}

static void Create(Program* program, mwinContext* context)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    CHECK(mwinCreateWindow(context, &def, &program->window, &program->request) == mwin_success,
          "create");
}

// Checks a phase's answer and starts the next phase's request.
static void Advance(Program* program, mwinContext* context, int outcome)
{
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(mwinRequestClipboardWrite(context, program->window, s_written, sizeof(s_written) - 1,
                                        &program->request) == mwin_success,
              "a write");
        break;
    case phaseWrite:
        CHECK(outcome == mwin_outcomeDone, "written");
        Read(program, context);
        break;
    case phaseRead:
        CHECK(outcome == mwin_outcomeDone && Found(context, s_written, sizeof(s_written) - 1),
              "read back, the NUL kept");
        Place(0);
        break;
    case phaseSurrogate:
        CHECK(outcome == mwin_outcomeDone && Found(context,
                                                   "A\xEF\xBF\xBD"
                                                   "B",
                                                   5),
              "a lone surrogate read as U+FFFD");
        Place(1);
        break;
    case phaseTooLarge:
        CHECK(outcome == mwin_outcomeTooLarge, "text past the limit too large");
        Refuse(true);
        Read(program, context);
        break;
    case phaseDenied:
        CHECK(outcome == mwin_outcomeDenied && Found(context,
                                                     "A\xEF\xBF\xBD"
                                                     "B",
                                                     5),
              "a refusal denied, the last text kept");
        Refuse(false);
        Read(program, context);
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "destroy");
        break;
    case phaseGone:
        CHECK(outcome == mwin_outcomeCancelled, "the read cancelled with its window");
        // The read's promise settles while the frames go by.
        program->waitFrames = 30;
        break;
    case phaseWait:
        Create(program, context);
        break;
    case phaseCreateAgain:
        CHECK(outcome == mwin_outcomeDone, "created again");
        Place(2);
        break;
    default:
        CHECK(outcome == mwin_outcomeDone && Found(context, "last", 4),
              "the next window reads the clipboard's text now");
        break;
    }
    program->phase += 1;
    program->reading = false;
    program->startMs = mwinWebNow();
}

// Whether the phase is ready to go on: its request answered, after the
// page's text is placed for those that read it.
static bool Ready(Program* program, mwinContext* context, int outcome)
{
    switch (program->phase)
    {
    case phaseSurrogate:
    case phaseTooLarge:
    case phaseAgain:
        if (!program->reading && Placed())
        {
            Read(program, context);
            program->reading = true;
        }
        return program->reading && outcome >= 0;
    case phaseWait:
        return program->waitFrames-- <= 0;
    default:
        return outcome >= 0;
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = mwinWebNow();
    Create(program, context);
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    int outcome = Outcome(program, context);
    if (Ready(program, context, outcome))
    {
        Advance(program, context, outcome);
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
    def.context.limits.clipboardBytes = LIMIT;
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
