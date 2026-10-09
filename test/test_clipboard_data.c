// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard's data and the primary selection on the test backend
// (mwin-0029): items refused at the call for a type that is not a MIME
// type, comes twice or is text that is not UTF-8, for no items or too
// many, and past the limit in all; a write's data on the platform's
// clipboard with its text item as the text; another program's data read
// by type, a type missing failing and the data found kept, data past
// the limit too large; text/plain refused for a data read; a text write
// taking the data away; the primary selection written and read apart
// from the clipboard, repaired, too large.

#include "test_program.h"

#include "maul-window/clipboard.h"
#include "maul-window/test.h"

#include <stdlib.h>
#include <string.h>

#define LIMIT 16

static int Outcome(const Program* program, int request)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        if (event->type == mwin_eventRequestCompleted &&
            SameId(event->data.completion.request, program->requests[request]))
        {
            return event->data.completion.outcome;
        }
    }
    return -1;
}

static mwinClipboardItem Item(const char* mime, const char* bytes, size_t length)
{
    return (mwinClipboardItem){mime, strlen(mime), bytes, length};
}

static bool Refused(mwinContext* context, mwinWindowId window, const mwinClipboardItem* items,
                    uint32_t count)
{
    return mwinRequestClipboardWriteData(context, window, items, count, nullptr) ==
           mwin_errorInvalid;
}

static void CheckRefusals(mwinContext* context, mwinWindowId window)
{
    mwinClipboardItem noSlash = Item("imagepng", "a", 1);
    mwinClipboardItem empty = Item("image/", "a", 1);
    mwinClipboardItem control = Item("image/p\nng", "a", 1);
    mwinClipboardItem twice[2] = {Item("image/png", "a", 1), Item("IMAGE/PNG", "b", 1)};
    mwinClipboardItem texts[2] = {Item("text/plain", "a", 1),
                                  Item("text/plain;charset=utf-8", "b", 1)};
    mwinClipboardItem badText = Item("text/plain", "a\xC3", 2);
    mwinClipboardItem five[5] = {Item("a/a", "", 0), Item("a/b", "", 0), Item("a/c", "", 0),
                                 Item("a/d", "", 0), Item("a/e", "", 0)};
    char longType[MWIN_CLIPBOARD_MIME + 2];
    memset(longType, 'a', sizeof(longType));
    longType[1] = '/';
    mwinClipboardItem tooLong = {longType, sizeof(longType) - 1, "a", 1};
    mwinClipboardItem pastLimit[2] = {Item("image/png", "0123456789", 10),
                                      Item("text/plain", "0123456", 7)};
    CHECK(Refused(context, window, &noSlash, 1) && Refused(context, window, &empty, 1) &&
              Refused(context, window, &control, 1) && Refused(context, window, twice, 2) &&
              Refused(context, window, texts, 2) && Refused(context, window, &badText, 1) &&
              Refused(context, window, five, 5) && Refused(context, window, five, 0) &&
              Refused(context, window, nullptr, 1) && Refused(context, window, &tooLong, 1),
          "items refused at the call");
    CHECK(mwinRequestClipboardWriteData(context, window, pastLimit, 2, nullptr) ==
              mwin_errorCapacity,
          "past the limit in all");
    // Types alike but for case, Z included, each alone in a heap
    // allocation of its length so that AddressSanitizer sees a read past.
    char* lower = malloc(3);
    char* upper = malloc(3);
    if (lower != nullptr && upper != nullptr)
    {
        memcpy(lower, "a/z", 3);
        memcpy(upper, "A/Z", 3);
        mwinClipboardItem alike[2] = {{lower, 3, "a", 1}, {upper, 3, "b", 1}};
        CHECK(Refused(context, window, alike, 2), "a type twice, told apart only by case");
    }
    free(lower);
    free(upper);
    mwinClipboardItem atLimit[2] = {Item("image/png", "0123456789", 10),
                                    Item("text/plain", "012345", 6)};
    CHECK(mwinRequestClipboardWriteData(context, window, atLimit, 2, nullptr) == mwin_success,
          "the limit in all exactly");
    CHECK(mwinRequestClipboardReadData(context, window, "text/plain", 10, nullptr) ==
                  mwin_errorInvalid &&
              mwinRequestClipboardReadData(context, window, "TEXT/PLAIN; charset=utf-8", 25,
                                           nullptr) == mwin_errorInvalid &&
              mwinRequestClipboardReadData(context, window, "png", 3, nullptr) == mwin_errorInvalid,
          "a data read refused for text/plain or a type that is not one");
}

static bool PlatformHas(mwinContext* context, const char* mime, const char* expected, size_t length)
{
    char bytes[LIMIT];
    size_t found = 0;
    return mwinTestGetClipboardData(context, mime, strlen(mime), bytes, sizeof(bytes), &found) ==
               mwin_success &&
           found == length && memcmp(bytes, expected, length) == 0;
}

static bool PlatformText(mwinContext* context, const char* expected)
{
    char text[LIMIT];
    size_t length = 0;
    return mwinTestGetClipboard(context, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static bool FoundData(mwinContext* context, mwinRequestId read, const char* expected, size_t length)
{
    char bytes[LIMIT];
    size_t found = 0;
    return mwinGetClipboardData(context, read, bytes, sizeof(bytes), &found) == mwin_success &&
           found == length && memcmp(bytes, expected, length) == 0;
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinWindowId window = program->windows[0];
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, &program->requests[0]);
        break;
    case 1:
    {
        CheckRefusals(context, window);
        mwinClipboardItem items[3] = {Item("text/plain", "hi", 2), Item("image/png", "\x89PNG", 4),
                                      Item("application/x-maul", "abc", 3)};
        CHECK(mwinRequestClipboardWriteData(context, window, items, 3, &program->requests[1]) ==
                  mwin_success,
              "a write of text and two types");
        break;
    }
    case 2:
        CHECK(Outcome(program, 1) == mwin_outcomeDone && PlatformText(context, "hi") &&
                  PlatformHas(context, "image/png", "\x89PNG", 4) &&
                  PlatformHas(context, "application/x-maul", "abc", 3),
              "the data on the platform's clipboard, its text item as the text");
        CHECK(mwinTestSetClipboardData(context, "image/png", 9, "PNG2", 4) == mwin_success &&
                  mwinRequestClipboardReadData(context, window, "image/png", 9,
                                               &program->requests[2]) == mwin_success &&
                  mwinRequestClipboardRead(context, window, &program->requests[3]) == mwin_success,
              "another program's data read, and the text");
        break;
    case 3:
    {
        char text[4];
        size_t length = 99;
        CHECK(Outcome(program, 2) == mwin_outcomeDone &&
                  FoundData(context, program->requests[2], "PNG2", 4) &&
                  Outcome(program, 3) == mwin_outcomeDone &&
                  mwinGetClipboardText(context, program->requests[3], text, sizeof(text),
                                       &length) == mwin_success &&
                  length == 0,
              "its data found, its text gone with the copy");
        CHECK(mwinRequestClipboardReadData(context, window, "image/jpeg", 10,
                                           &program->requests[4]) == mwin_success,
              "a type it lacks");
        break;
    }
    case 4:
        CHECK(Outcome(program, 4) == mwin_outcomeFailed &&
                  FoundData(context, program->requests[2], "PNG2", 4),
              "failed, the data found kept");
        CHECK(mwinTestSetClipboardData(context, "image/png", 9, "0123456789abcdefg", LIMIT + 1) ==
                      mwin_success &&
                  mwinRequestClipboardReadData(context, window, "image/png", 9,
                                               &program->requests[5]) == mwin_success,
              "data past the limit");
        break;
    case 5:
        CHECK(Outcome(program, 5) == mwin_outcomeTooLarge &&
                  FoundData(context, program->requests[2], "PNG2", 4),
              "too large, the data found kept");
        CHECK(mwinRequestClipboardWrite(context, window, "only", 4, &program->requests[6]) ==
                  mwin_success,
              "a text write");
        break;
    case 6:
    {
        size_t length = 0;
        CHECK(Outcome(program, 6) == mwin_outcomeDone && PlatformText(context, "only") &&
                  mwinTestGetClipboardData(context, "image/png", 9, nullptr, 0, &length) ==
                      mwin_errorInvalid,
              "the text alone, the data gone");
        CHECK(mwinRequestPrimaryWrite(context, window, nullptr, 0, nullptr) == mwin_success &&
                  mwinRequestPrimaryWrite(context, window, "0123456789abcdef", LIMIT, nullptr) ==
                      mwin_success,
              "an empty primary selection, and one of the limit exactly");
        CHECK(mwinRequestPrimaryWrite(context, window, "sel", 3, &program->requests[7]) ==
                      mwin_success &&
                  mwinRequestPrimaryWrite(context, window, "a\xC3", 2, nullptr) ==
                      mwin_errorInvalid &&
                  mwinRequestPrimaryWrite(context, window, "0123456789abcdefg", LIMIT + 1,
                                          nullptr) == mwin_errorCapacity,
              "the primary selection written; refused when not UTF-8 or past the limit");
        break;
    }
    case 7:
    {
        char text[LIMIT];
        size_t length = 0;
        CHECK(Outcome(program, 7) == mwin_outcomeDone &&
                  mwinTestGetPrimary(context, text, sizeof(text), &length) == mwin_success &&
                  length == 3 && memcmp(text, "sel", 3) == 0 && PlatformText(context, "only") &&
                  mwinTestGetPrimary(context, text, 3, &length) == mwin_success,
              "the primary selection apart from the clipboard");
        CHECK(mwinTestSetPrimary(context, "a\xC3(", 3) == mwin_success &&
                  mwinRequestPrimaryRead(context, window, &program->requests[8]) == mwin_success,
              "another program's selection read");
        break;
    }
    case 8:
    {
        char text[LIMIT];
        size_t length = 0;
        CHECK(Outcome(program, 8) == mwin_outcomeDone &&
                  mwinGetPrimaryText(context, program->requests[8], text, sizeof(text), &length) ==
                      mwin_success &&
                  length == 5 && memcmp(text, "a\xEF\xBF\xBD(", 5) == 0,
              "repaired");
        // The limit exactly, one byte of it not UTF-8: three once repaired.
        CHECK(mwinTestSetPrimary(context,
                                 "\xFF"
                                 "abcdefghijklmno",
                                 LIMIT) == mwin_success &&
                  mwinRequestPrimaryRead(context, window, &program->requests[9]) == mwin_success,
              "a selection past the limit once repaired");
        break;
    }
    case 9:
    {
        char text[LIMIT];
        size_t length = 0;
        CHECK(Outcome(program, 9) == mwin_outcomeTooLarge &&
                  mwinGetPrimaryText(context, program->requests[8], text, sizeof(text), &length) ==
                      mwin_success &&
                  length == 5 &&
                  mwinGetPrimaryText(context, program->requests[9], text, sizeof(text), &length) ==
                      mwin_errorStale,
              "too large, the text found kept for the read that found it");
        program->windows[1] = Create(context, &program->requests[12]);
        break;
    }
    case 10:
    {
        // Another window's reads: only the numbers tell the first's apart.
        window = program->windows[1];
        CHECK(mwinTestSetPrimary(context, "two", 3) == mwin_success &&
                  mwinRequestPrimaryRead(context, window, &program->requests[10]) == mwin_success &&
                  mwinTestSetClipboardData(context, "image/png", 9, "PNG3", 4) == mwin_success &&
                  mwinRequestClipboardReadData(context, window, "image/png", 9,
                                               &program->requests[11]) == mwin_success,
              "the selection and the data read again, by another window");
        break;
    }
    default:
    {
        char text[LIMIT];
        size_t length = 0;
        CHECK(Outcome(program, 10) == mwin_outcomeDone &&
                  Outcome(program, 11) == mwin_outcomeDone &&
                  mwinGetPrimaryText(context, program->requests[8], text, sizeof(text), &length) ==
                      mwin_errorStale &&
                  mwinGetClipboardData(context, program->requests[2], text, sizeof(text),
                                       &length) == mwin_errorStale &&
                  mwinGetPrimaryText(context, program->requests[10], text, sizeof(text), &length) ==
                      mwin_success &&
                  length == 3 && memcmp(text, "two", 3) == 0 &&
                  FoundData(context, program->requests[11], "PNG3", 4),
              "the first window's selection and data stale once another window read them");
        program->done = true;
        break;
    }
    }
}

int main(void)
{
    Program program = {.step = Step};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.clipboardBytes = LIMIT;
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
