// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on macOS against AppKit (a CI runner's session): a
// window that does not accept text takes only Roman keyboard layouts;
// one that does takes every input source, and has a caret the candidate
// window goes by; marked text with
// two clauses, the thick-underlined one the target, comes as a
// composition; committing ends it with the text; turning text input off
// ends a composition without committing it. No input method can be
// driven on the runner, so the test calls the view's text input client
// as a method would.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull
#define MAX_RECORDS 16

// か, な and を; 仮 and 名.
#define KA   "\xE3\x81\x8B"
#define NA   "\xE3\x81\xAA"
#define WO   "\xE3\x82\x92"
#define KARI "\xE4\xBB\xAE"
#define MEI  "\xE5\x90\x8D"

typedef struct Record
{
    mwinEventType type;
    char text[32];
    uint32_t length;
    int32_t caret;
    uint32_t selectionStart;
    uint32_t selectionEnd;
    mwinPreeditSegment segments[4];
    uint32_t segmentCount;
    mwinOutcome outcome;
} Record;

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    Record records[MAX_RECORDS];
    size_t count;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void Keep(Record* record, const mwinEvent* event)
{
    if (event->type == mwin_eventRequestCompleted)
    {
        record->outcome = event->data.completion.outcome;
        return;
    }
    const char* text =
        event->type == mwin_eventTextInput ? event->data.text.text : event->data.preedit.text;
    uint32_t length =
        event->type == mwin_eventTextInput ? event->data.text.length : event->data.preedit.length;
    record->length = length;
    memcpy(record->text, text, length < 31 ? length : 31);
    if (event->type != mwin_eventImePreedit)
    {
        return;
    }
    const mwinPreeditEvent* preedit = &event->data.preedit;
    record->caret = preedit->caret;
    record->selectionStart = preedit->selectionStart;
    record->selectionEnd = preedit->selectionEnd;
    record->segmentCount = preedit->segmentCount;
    for (uint32_t i = 0; i < preedit->segmentCount && i < 4; i++)
    {
        record->segments[i] = preedit->segments[i];
    }
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        bool kept = event.type == mwin_eventTextInput || event.type == mwin_eventImePreedit ||
                    event.type == mwin_eventRequestCompleted;
        if (kept && program->count < MAX_RECORDS)
        {
            Record* record = &program->records[program->count++];
            *record = (Record){.type = event.type};
            Keep(record, &event);
        }
    }
}

static bool RomanOnly(NSView* view)
{
    return [view.inputContext.allowedInputSourceLocales
        isEqual:@[ NSAllRomanInputSourcesLocaleIdentifier ]];
}

static const Record* Find(const Program* program, size_t* at, mwinEventType type)
{
    for (; *at < program->count; (*at)++)
    {
        if (program->records[*at].type == type)
        {
            return &program->records[(*at)++];
        }
    }
    return nullptr;
}

static bool Is(const Record* record, const char* text)
{
    return record != nullptr && record->length == strlen(text) &&
           memcmp(record->text, text, record->length) == 0;
}

static NSView<NSTextInputClient>* ViewOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (NSView<NSTextInputClient>*)handles.handles.apple.view
               : nil;
}

// Two clauses as an input method marks them: か and な to convert, を
// the target, with the method's selection on を.
static void MarkClauses(NSView<NSTextInputClient>* view)
{
    NSMutableAttributedString* marked =
        [[NSMutableAttributedString alloc] initWithString:@"かなを"];
    [marked addAttributes:@{
        NSMarkedClauseSegmentAttributeName : @0,
        NSUnderlineStyleAttributeName : @(NSUnderlineStyleSingle)
    }
                    range:NSMakeRange(0, 2)];
    [marked addAttributes:@{
        NSMarkedClauseSegmentAttributeName : @1,
        NSUnderlineStyleAttributeName : @(NSUnderlineStyleThick)
    }
                    range:NSMakeRange(2, 1)];
    [view setMarkedText:marked
           selectedRange:NSMakeRange(2, 1)
        replacementRange:NSMakeRange(NSNotFound, 0)];
    [marked release];
}

static void CheckClauses(const Program* program, NSView<NSTextInputClient>* view)
{
    size_t at = 0;
    const Record* preedit = Find(program, &at, mwin_eventImePreedit);
    CHECK(Is(preedit, KA NA WO) && preedit->segmentCount == 2 && preedit->segments[0].start == 0 &&
              preedit->segments[0].length == 6 &&
              preedit->segments[0].style == mwin_preeditUnderline &&
              preedit->segments[1].start == 6 && preedit->segments[1].length == 3 &&
              preedit->segments[1].style == mwin_preeditTarget,
          "a composition's clauses in bytes, the thick-underlined one the target");
    CHECK(preedit != nullptr && preedit->selectionStart == 6 && preedit->selectionEnd == 9 &&
              preedit->caret == 9,
          "the target as the selection, the caret after it");
    CHECK(view.hasMarkedText && view.markedRange.length == 3 && view.selectedRange.location == 2,
          "the view keeps the marked text for the method");
}

static void CheckCommit(const Program* program, NSView<NSTextInputClient>* view)
{
    size_t at = 0;
    const Record* text = Find(program, &at, mwin_eventTextInput);
    const Record* end = Find(program, &at, mwin_eventImePreedit);
    CHECK(Is(text, KARI MEI WO), "the committed text");
    CHECK(end != nullptr && end->length == 0 && end->caret == -1, "after it, the composition ends");
    CHECK(!view.hasMarkedText, "no marked text is left");
}

static void CheckPlain(const Program* program)
{
    size_t at = 0;
    const Record* preedit = Find(program, &at, mwin_eventImePreedit);
    CHECK(Is(preedit, KA) && preedit->segmentCount == 1 &&
              preedit->segments[0].style == mwin_preeditUnderline && preedit->caret == 3,
          "plain marked text: one clause to convert");
}

// A composition is state: the end replaces one still waiting, so the
// plain one was read a frame before.
static void CheckOff(const Program* program, NSView<NSTextInputClient>* view)
{
    size_t at = 0;
    const Record* end = Find(program, &at, mwin_eventImePreedit);
    CHECK(end != nullptr && end->length == 0, "turning text input off ends the composition");
    size_t textAt = 0;
    CHECK(Find(program, &textAt, mwin_eventTextInput) == nullptr, "without committing it");
    size_t doneAt = 0;
    const Record* done = Find(program, &doneAt, mwin_eventRequestCompleted);
    CHECK(done != nullptr && done->outcome == mwin_outcomeDone && RomanOnly(view),
          "and only Roman layouts are taken again");
}

// Each phase acts at once, and the next frame reads what it posted.
static void Advance(Program* program, mwinContext* context)
{
    NSView<NSTextInputClient>* view = ViewOf(context, program->window);
    switch (program->phase)
    {
    case 0:
        CHECK(view != nil && RomanOnly(view), "only Roman layouts without text input");
        CHECK(mwinRequestTextInput(context, program->window, true,
                                   (mwinRect){10.0f, 20.0f, 2.0f, 16.0f}, nullptr) == mwin_success,
              "text input on");
        break;
    case 1:
    {
        size_t at = 0;
        const Record* done = Find(program, &at, mwin_eventRequestCompleted);
        NSRect caret = [view firstRectForCharacterRange:NSMakeRange(0, 0) actualRange:nullptr];
        CHECK(done != nullptr && done->outcome == mwin_outcomeDone &&
                  view.inputContext.allowedInputSourceLocales == nil,
              "every input source with text input");
        CHECK(caret.size.width == 2.0 && caret.size.height == 16.0,
              "the candidate window goes by the caret");
        MarkClauses(view);
        break;
    }
    case 2:
        CheckClauses(program, view);
        [view insertText:@"仮名を" replacementRange:NSMakeRange(NSNotFound, 0)];
        break;
    case 3:
        CheckCommit(program, view);
        [view setMarkedText:@"か"
               selectedRange:NSMakeRange(1, 0)
            replacementRange:NSMakeRange(NSNotFound, 0)];
        break;
    case 4:
        CheckPlain(program);
        CHECK(mwinRequestTextInput(context, program->window, false, (mwinRect){0}, nullptr) ==
                  mwin_success,
              "text input off");
        break;
    case 5:
        CheckOff(program, view);
        break;
    default:
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startNs = NowNs();
}

// Phases 1 and 5 wait for their request's completion.
static bool Pending(const Program* program)
{
    size_t at = 0;
    return (program->phase == 1 || program->phase == 5) &&
           Find(program, &at, mwin_eventRequestCompleted) == nullptr &&
           NowNs() - program->startNs < DEADLINE_NS;
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS input methods";
    def.titleLength = 24;
    def.size = (mwinSize){320.0f, 240.0f};
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (program->shown && !Pending(program))
    {
        @autoreleasepool
        {
            Advance(program, context);
        }
    }
    else if (!program->shown && NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == 6 ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {0};
    setvbuf(stdout, nullptr, _IONBF, 0);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on macOS");
    CHECK(!program.timedOut, "the window shows in time");
    CHECK(program.phase == 6, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
