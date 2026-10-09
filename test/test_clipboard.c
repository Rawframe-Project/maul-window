// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard's contract on the test backend: a write copied at the
// call and put on the platform's clipboard at the next pump, a later
// write superseding a waiting one; text refused at the call when it is
// not UTF-8 or past the limit, taken when empty or of the limit
// exactly; a read's text copied out, UTF-8 and UTF-16 of the limit
// exactly whole, ill-formed
// UTF-8 and UTF-16 repaired, too large before and after repair, the
// text of the last done read kept through a refusal or a read too
// large; an empty clipboard read as empty; a read cancelled with its
// window.

#include "test_program.h"

#include "maul-window/clipboard.h"
#include "maul-window/test.h"

#include <string.h>

#define LIMIT 16

// The outcome of a request's completion among the drained records, or
// -1.
static int Outcome(const Program* program, int request, mwinRequestKind kind)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        if (event->type == mwin_eventRequestCompleted &&
            SameId(event->data.completion.request, program->requests[request]))
        {
            return event->data.completion.kind == kind ? event->data.completion.outcome : -1;
        }
    }
    return -1;
}

static bool Found(mwinContext* context, mwinRequestId read, const char* expected)
{
    char text[64];
    size_t length = 0;
    return mwinGetClipboardText(context, read, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static void Read(Program* program, mwinContext* context, int request)
{
    CHECK(mwinRequestClipboardRead(context, program->windows[0], &program->requests[request]) ==
              mwin_success,
          "a read");
}

static void Write(Program* program, mwinContext* context)
{
    mwinWindowId window = program->windows[0];
    CHECK(mwinRequestClipboardWrite(context, window, nullptr, 0, nullptr) == mwin_success &&
              mwinRequestClipboardWrite(context, window, "0123456789abcdef", LIMIT, nullptr) ==
                  mwin_success,
          "empty text, and text of the limit exactly");
    CHECK(mwinRequestClipboardWrite(context, window, "h\xC3\xA9llo", 6, &program->requests[1]) ==
                  mwin_success &&
              mwinRequestClipboardWrite(context, window, "second", 6, &program->requests[2]) ==
                  mwin_success,
          "two writes");
    CHECK(mwinRequestClipboardWrite(context, window, "a\xC3", 2, nullptr) == mwin_errorInvalid &&
              mwinRequestClipboardWrite(context, window, "0123456789abcdefg", LIMIT + 1, nullptr) ==
                  mwin_errorCapacity &&
              mwinRequestClipboardWrite(nullptr, window, "a", 1, nullptr) == mwin_errorInvalid &&
              mwinRequestClipboardWrite(context, window, nullptr, 1, nullptr) ==
                  mwin_errorInvalid &&
              mwinRequestClipboardRead(context, (mwinWindowId){0}, nullptr) == mwin_errorStale,
          "text not UTF-8 or past the limit refused at the call, and a stale window");
}

static void CheckWritten(Program* program, mwinContext* context)
{
    char text[16];
    size_t length = 0;
    CHECK(Outcome(program, 1, mwin_requestClipboardWrite) == mwin_outcomeSuperseded &&
              Outcome(program, 2, mwin_requestClipboardWrite) == mwin_outcomeDone &&
              mwinTestGetClipboard(context, text, sizeof(text), &length) == mwin_success &&
              length == 6 && memcmp(text, "second", 6) == 0,
          "the later write on the clipboard, the first superseded");
    // A lone lead byte before '(', and a four-byte sequence cut short.
    CHECK(mwinTestSetClipboard(context, "a\xC3(b\xF0\x9F\x98", 7) == mwin_success, "set");
    Read(program, context, 3);
}

static void CheckRepaired(Program* program, mwinContext* context)
{
    CHECK(Outcome(program, 3, mwin_requestClipboardRead) == mwin_outcomeDone &&
              Found(context, program->requests[3], "a\xEF\xBF\xBD(b\xEF\xBF\xBD"),
          "each maximal ill-formed subpart replaced");
    char text[2];
    size_t length = 0;
    mwinRequestId read = program->requests[3];
    CHECK(mwinGetClipboardText(context, read, text, sizeof(text), &length) == mwin_errorCapacity &&
              length == 9 && text[0] == 'a' && (unsigned char)text[1] == 0xEF &&
              mwinGetClipboardText(context, read, nullptr, 0, &length) == mwin_errorCapacity &&
              mwinGetClipboardText(context, read, nullptr, 1, &length) == mwin_errorInvalid,
          "the bytes that fit, and the length");
    CHECK(mwinGetClipboardText(context, (mwinRequestId){0}, nullptr, 0, &length) ==
                  mwin_errorStale &&
              mwinGetClipboardText(context, program->requests[2], nullptr, 0, &length) ==
                  mwin_errorStale,
          "the null request and a write have no text");
    static const uint16_t units[] = {0x48, 0xD83D, 0xDE00, 0xD800, 0x21};
    CHECK(mwinTestSetClipboardUtf16(context, units, 5) == mwin_success, "set UTF-16");
    Read(program, context, 4);
}

static void CheckUtf16(Program* program, mwinContext* context)
{
    CHECK(Outcome(program, 4, mwin_requestClipboardRead) == mwin_outcomeDone &&
              Found(context, program->requests[4], "H\xF0\x9F\x98\x80\xEF\xBF\xBD!"),
          "UTF-16 as UTF-8, a lone surrogate replaced");
    CHECK(mwinTestSetClipboard(context, "0123456789abcdefg", LIMIT + 1) == mwin_success, "set");
    Read(program, context, 5);
}

static void CheckTooLarge(Program* program, mwinContext* context)
{
    CHECK(Outcome(program, 5, mwin_requestClipboardRead) == mwin_outcomeTooLarge &&
              Found(context, program->requests[4], "H\xF0\x9F\x98\x80\xEF\xBF\xBD!"),
          "text past the limit too large, the last text kept");
    // Six bytes, eighteen once repaired.
    CHECK(mwinTestSetClipboard(context, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) == mwin_success, "set");
    Read(program, context, 6);
}

static void CheckRepairedTooLarge(Program* program, mwinContext* context)
{
    CHECK(Outcome(program, 6, mwin_requestClipboardRead) == mwin_outcomeTooLarge,
          "text past the limit once repaired too large");
    CHECK(mwinTestSetAnswer(context, mwin_requestClipboardRead, mwin_outcomeDenied) == mwin_success,
          "deny");
    Read(program, context, 7);
}

static void CheckDenied(Program* program, mwinContext* context)
{
    CHECK(Outcome(program, 7, mwin_requestClipboardRead) == mwin_outcomeDenied &&
              Found(context, program->requests[4], "H\xF0\x9F\x98\x80\xEF\xBF\xBD!"),
          "a refused read, the last text kept");
    CHECK(mwinTestSetAnswer(context, mwin_requestClipboardRead, mwin_outcomeDone) == mwin_success &&
              mwinTestSetClipboard(context, nullptr, 0) == mwin_success,
          "an empty clipboard");
    Read(program, context, 8);
}

static void CheckEmpty(Program* program, mwinContext* context)
{
    size_t length = 0;
    CHECK(Outcome(program, 8, mwin_requestClipboardRead) == mwin_outcomeDone &&
              Found(context, program->requests[8], ""),
          "an empty clipboard read as empty text");
    CHECK(mwinGetClipboardText(context, program->requests[4], nullptr, 0, &length) ==
                  mwin_errorStale &&
              mwinGetClipboardText(context, program->requests[7], nullptr, 0, &length) ==
                  mwin_errorStale,
          "a read whose text a later one replaced, and a refused read, stale");
    Read(program, context, 9);
    CHECK(mwinDestroyWindow(context, program->windows[0]) == mwin_success, "destroy");
}

// Two windows read in one frame, both answered before the program looks.
static void CheckTwoWindows(Program* program, mwinContext* context)
{
    CHECK(mwinTestSetClipboard(context, "one", 3) == mwin_success &&
              mwinRequestClipboardRead(context, program->windows[1], &program->requests[12]) ==
                  mwin_success &&
              mwinRequestClipboardRead(context, program->windows[2], &program->requests[13]) ==
                  mwin_success,
          "two windows' reads");
}

static void CheckOnlyTheLast(Program* program, mwinContext* context)
{
    size_t length = 0;
    CHECK(Outcome(program, 12, mwin_requestClipboardRead) == mwin_outcomeDone &&
              Outcome(program, 13, mwin_requestClipboardRead) == mwin_outcomeDone &&
              mwinGetClipboardText(context, program->requests[12], nullptr, 0, &length) ==
                  mwin_errorStale &&
              Found(context, program->requests[13], "one"),
          "the first window's read stale once the second's found text, never given it");
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, &program->requests[0]);
        break;
    case 1:
        Write(program, context);
        break;
    case 2:
        CheckWritten(program, context);
        break;
    case 3:
        CheckRepaired(program, context);
        break;
    case 4:
        CheckUtf16(program, context);
        break;
    case 5:
        CheckTooLarge(program, context);
        break;
    case 6:
        CheckRepairedTooLarge(program, context);
        break;
    case 7:
        CheckDenied(program, context);
        break;
    case 8:
        CheckEmpty(program, context);
        break;
    case 9:
        CHECK(Outcome(program, 9, mwin_requestClipboardRead) == mwin_outcomeCancelled,
              "a read cancelled with its window");
        program->windows[1] = Create(context, &program->requests[10]);
        program->windows[2] = Create(context, &program->requests[11]);
        break;
    case 10:
        CheckTwoWindows(program, context);
        break;
    default:
        CheckOnlyTheLast(program, context);
        program->done = true;
        break;
    }
}

// Text of the limit exactly, in UTF-8 and in UTF-16, is read whole and
// copied into a buffer of its length.
static void ExactStep(Program* program, mwinContext* context, int step)
{
    static const char text[] = "0123456789abcdef";
    static const uint16_t units[LIMIT] = {'f', 'e', 'd', 'c', 'b', 'a', '9', '8',
                                          '7', '6', '5', '4', '3', '2', '1', '0'};
    char found[LIMIT];
    size_t length = 0;
    Drain(program, context);
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        break;
    case 1:
        CHECK(mwinTestSetClipboard(context, text, LIMIT) == mwin_success, "set");
        Read(program, context, 0);
        break;
    case 2:
        CHECK(Outcome(program, 0, mwin_requestClipboardRead) == mwin_outcomeDone &&
                  mwinGetClipboardText(context, program->requests[0], found, sizeof(found),
                                       &length) == mwin_success &&
                  length == LIMIT && memcmp(found, text, LIMIT) == 0,
              "UTF-8 of the limit exactly");
        CHECK(mwinTestSetClipboardUtf16(context, units, LIMIT) == mwin_success, "set UTF-16");
        Read(program, context, 1);
        break;
    default:
        CHECK(Outcome(program, 1, mwin_requestClipboardRead) == mwin_outcomeDone &&
                  mwinGetClipboardText(context, program->requests[1], found, sizeof(found),
                                       &length) == mwin_success &&
                  length == LIMIT && memcmp(found, "fedcba9876543210", LIMIT) == 0,
              "UTF-16 of the limit exactly");
        program->done = true;
        break;
    }
}

int main(void)
{
    Program exact = {.step = ExactStep};
    mwinContextDef exactDef = mwinDefaultContextDef();
    exactDef.limits.clipboardBytes = LIMIT;
    CHECK(RunWith(&exact, exactDef) == mwin_success && exact.done, "the exact program runs");
    Program program = {.step = Step};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.clipboardBytes = LIMIT;
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
