// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's clipboard in headless Chrome (test/web_runner.cjs
// grants the page the clipboard): text written, a NUL within it, and
// read back; text the page put there with a lone surrogate read with
// U+FFFD; text of the limit read whole, text past it too large; a
// refusal denied, the last text kept; a read whose window went answered
// for no one, its text taken, so the next window's read has the
// clipboard's text now, and reading it again holds no more memory. Data
// (mwin-0029): a custom type, a PNG and text written together; the
// custom type read back as it was, the PNG as a PNG (the browser encodes
// it again), the text as text; a type the clipboard lacks fails. Writes,
// data writes and data reads refused denied; each unsupported on a page
// without the clipboard's API; the primary selection is unsupported.

#include "counting_allocator.h"
#include "test_harness.h"
#include "web_js.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"

#include <string.h>

#define DEADLINE_MS 10000.0
#define LIMIT       256

typedef enum Phase
{
    phaseCreate,
    phaseWrite,
    phaseRead,
    phaseSurrogate,
    phaseExact,
    phaseTooLarge,
    phaseDenied,
    phaseGone,
    phaseWait,
    phaseCreateAgain,
    phaseAgain,
    phaseSame,
    phaseWriteData,
    phaseReadCustom,
    phaseReadPng,
    phaseReadText,
    phaseMissing,
    phaseWriteDenied,
    phaseDataWriteDenied,
    phaseDataReadDenied,
    phaseNoWrite,
    phaseNoRead,
    phaseNoDataWrite,
    phaseNoDataRead,
    phasePrimary,
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
    const texts = ['A\uD800B', 'x'.repeat(300), 'last', 'x'.repeat(256)];
    globalThis.mwinPlaced = false;
    navigator.clipboard.writeText(texts[which]).then(() => globalThis.mwinPlaced = true);
});

EM_JS(bool, Placed, (void), {
    return globalThis.mwinPlaced === true;
});

// The page's clipboard as it is (0), refusing everything as a page
// without permission would (1), or without its API (2).
EM_JS(void, SetClipboard, (int how), {
    const refuse = () => Promise.reject(new DOMException('refused', 'NotAllowedError'));
    for (const name of ['readText', 'writeText', 'read', 'write']) {
        if (how === 0) {
            delete navigator.clipboard[name];
        } else {
            navigator.clipboard[name] = how === 1 ? refuse : undefined;
        }
    }
});
// clang-format on

// The bytes the context held after the first read.
static size_t s_baseline;

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

// Whether the clipboard's text is the limit's bytes of x.
static bool FoundExact(mwinContext* context)
{
    char expected[LIMIT];
    memset(expected, 'x', sizeof(expected));
    return Found(context, expected, sizeof(expected));
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

// A PNG of one transparent pixel, and bytes of a type of the program's.
static const uint8_t s_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
    0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99,
    0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
static const uint8_t s_custom[] = {'o', 't', 'h', 'e', 'r', 0, 0xff};

// Whether the last data read found bytes beginning as these, and
// exactly these when whole.
static bool FoundData(mwinContext* context, const uint8_t* expected, size_t length, bool whole)
{
    uint8_t data[LIMIT];
    size_t found = 0;
    return mwinGetClipboardData(context, data, sizeof(data), &found) == mwin_success &&
           (whole ? found == length : found >= length) && memcmp(data, expected, length) == 0;
}

static void ReadData(Program* program, mwinContext* context, const char* mime)
{
    CHECK(mwinRequestClipboardReadData(context, program->window, mime, strlen(mime),
                                       &program->request) == mwin_success,
          "a data read");
}

static void Write(Program* program, mwinContext* context)
{
    CHECK(mwinRequestClipboardWrite(context, program->window, "no", 2, &program->request) ==
              mwin_success,
          "a write");
}

static void WriteData(Program* program, mwinContext* context)
{
    mwinClipboardItem item = {"image/png", 9, s_png, sizeof(s_png)};
    CHECK(mwinRequestClipboardWriteData(context, program->window, &item, 1, &program->request) ==
              mwin_success,
          "a data write");
}

// The phases of a page that refuses, then of one without the API.
static void AdvanceRefused(Program* program, mwinContext* context, int outcome)
{
    switch (program->phase)
    {
    case phaseWriteDenied:
        CHECK(outcome == mwin_outcomeDenied, "a refused write denied");
        WriteData(program, context);
        break;
    case phaseDataWriteDenied:
        CHECK(outcome == mwin_outcomeDenied, "a refused data write denied");
        ReadData(program, context, "image/png");
        break;
    case phaseDataReadDenied:
        CHECK(outcome == mwin_outcomeDenied, "a refused data read denied");
        SetClipboard(2);
        Write(program, context);
        break;
    case phaseNoWrite:
        CHECK(outcome == mwin_outcomeUnsupported, "no write without the page's API");
        Read(program, context);
        break;
    case phaseNoRead:
        CHECK(outcome == mwin_outcomeUnsupported, "no read without it");
        WriteData(program, context);
        break;
    case phaseNoDataWrite:
        CHECK(outcome == mwin_outcomeUnsupported, "no data write without it");
        ReadData(program, context, "image/png");
        break;
    case phaseNoDataRead:
        CHECK(outcome == mwin_outcomeUnsupported, "no data read without it");
        SetClipboard(0);
        CHECK(mwinRequestPrimaryRead(context, program->window, &program->request) == mwin_success,
              "a primary read");
        break;
    default:
        CHECK(outcome == mwin_outcomeUnsupported, "no primary selection");
        break;
    }
}

// The data phases' checks and requests.
static void AdvanceData(Program* program, mwinContext* context, int outcome)
{
    switch (program->phase)
    {
    case phaseAgain:
        CHECK(outcome == mwin_outcomeDone && Found(context, "last", 4),
              "the next window reads the clipboard's text now");
        s_baseline = s_countedBytes;
        Read(program, context);
        break;
    case phaseSame:
    {
        CHECK(outcome == mwin_outcomeDone && Found(context, "last", 4) &&
                  s_countedBytes == s_baseline,
              "read again, holding no more memory");
        mwinClipboardItem items[] = {
            {"image/png", 9, s_png, sizeof(s_png)},
            {"text/plain", 10, "hi", 2},
            {"application/x-maul", 18, s_custom, sizeof(s_custom)},
        };
        CHECK(mwinRequestClipboardWriteData(context, program->window, items, 3,
                                            &program->request) == mwin_success,
              "a data write");
        break;
    }
    case phaseWriteData:
        CHECK(outcome == mwin_outcomeDone, "data written");
        ReadData(program, context, "application/x-maul");
        break;
    case phaseReadCustom:
        CHECK(outcome == mwin_outcomeDone && FoundData(context, s_custom, sizeof(s_custom), true),
              "a custom type read back as it was");
        ReadData(program, context, "image/png");
        break;
    case phaseReadPng:
        CHECK(outcome == mwin_outcomeDone && FoundData(context, s_png, 8, false),
              "the image read back as a PNG");
        Read(program, context);
        break;
    case phaseReadText:
        CHECK(outcome == mwin_outcomeDone && Found(context, "hi", 2), "the write's text read");
        ReadData(program, context, "image/gif");
        break;
    case phaseMissing:
        CHECK(outcome == mwin_outcomeFailed, "a type the clipboard lacks fails");
        SetClipboard(1);
        Write(program, context);
        break;
    default:
        AdvanceRefused(program, context, outcome);
        break;
    }
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
        Place(3);
        break;
    case phaseExact:
        CHECK(outcome == mwin_outcomeDone && FoundExact(context), "text of the limit read whole");
        Place(1);
        break;
    case phaseTooLarge:
        CHECK(outcome == mwin_outcomeTooLarge, "text past the limit too large");
        SetClipboard(1);
        Read(program, context);
        break;
    case phaseDenied:
        CHECK(outcome == mwin_outcomeDenied && FoundExact(context),
              "a refusal denied, the last text kept");
        SetClipboard(0);
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
        AdvanceData(program, context, outcome);
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
    case phaseExact:
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
    def.context.allocator = CountingAllocator();
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
