// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on macOS against AppKit (a CI runner's session): a
// window that does not accept text takes only Roman keyboard layouts;
// one that does takes every input source, and has a caret the candidate
// window goes by; marked text with
// two clauses, the thick-underlined one the target, comes as a
// composition; committing ends it with the text; text typed and composed
// with a lone surrogate arrives with U+FFFD in its place; turning text
// input off ends a composition without committing it. With a limit of 64
// bytes of text a window, a commit and then a composition of 65 bytes
// come as a reset each, with no text, and nothing is allocated for them.
// No input method can be driven on the runner, so the test calls the
// view's text input client as a method would.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/native.h"

#import <AppKit/AppKit.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 10000000000ull
#define MAX_RECORDS 16
#define LAST_PHASE  10
// The test's limit of text a window, and a text one byte past it.
#define TEXT_BYTES 64
#define TOO_LONG   @"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ012"

// Blocks asked of the allocator while counting.
static bool s_counting;
static int s_allocations;

static void* Allocate(size_t size, size_t alignment, void* user)
{
    (void)user;
    s_allocations += s_counting;
    // aligned_alloc takes no alignment under a pointer's.
    return alignment <= alignof(max_align_t)
               ? malloc(size)
               : aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
}

static void Release(void* memory, size_t size, size_t alignment, void* user)
{
    (void)size;
    (void)alignment;
    (void)user;
    free(memory);
}

// か, な and を; 仮 and 名.
#define KA   "\xE3\x81\x8B"
#define NA   "\xE3\x81\xAA"
#define WO   "\xE3\x82\x92"
#define KARI "\xE4\xBB\xAE"
#define MEI  "\xE5\x90\x8D"

typedef struct Record
{
    mwinEventKind kind;
    char text[32];
    size_t length;
    ptrdiff_t caret;
    size_t selectionStart;
    size_t selectionEnd;
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
    // Resets told since the phase began.
    int resets;
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
    if (event->kind == mwin_eventRequestCompleted)
    {
        record->outcome = event->data.completion.outcome;
        return;
    }
    const char* text =
        event->kind == mwin_eventTextInput ? event->data.text.text : event->data.preedit.text;
    size_t length =
        event->kind == mwin_eventTextInput ? event->data.text.length : event->data.preedit.length;
    record->length = length;
    memcpy(record->text, text, length < 31 ? length : 31);
    if (event->kind != mwin_eventImePreedit)
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
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.kind == mwin_eventShown;
        program->resets += event.kind == mwin_eventInputStateReset;
        bool kept = event.kind == mwin_eventTextInput || event.kind == mwin_eventImePreedit ||
                    event.kind == mwin_eventRequestCompleted;
        if (kept && program->count < MAX_RECORDS)
        {
            Record* record = &program->records[program->count++];
            *record = (Record){.kind = event.kind};
            Keep(record, &event);
        }
    }
}

static bool RomanOnly(NSView* view)
{
    return [view.inputContext.allowedInputSourceLocales
        isEqual:@[ NSAllRomanInputSourcesLocaleIdentifier ]];
}

static const Record* Find(const Program* program, size_t* at, mwinEventKind type)
{
    for (; *at < program->count; (*at)++)
    {
        if (program->records[*at].kind == type)
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
    return mwinWindow_GetNativeHandles(context, window, &handles) == mwin_success
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

// "x", a lone surrogate, "y", and "a", one, "b": what UTF8String cannot
// convert.
static NSString* Surrogate(unichar first, unichar last)
{
    const unichar units[] = {first, 0xD800, last};
    return [NSString stringWithCharacters:units length:3];
}

static void CheckSurrogateText(const Program* program)
{
    size_t at = 0;
    const Record* text = Find(program, &at, mwin_eventTextInput);
    CHECK(text != nullptr && text->length == 5 &&
              memcmp(text->text,
                     "x\xEF\xBF\xBD"
                     "y",
                     5) == 0,
          "typed text with a lone surrogate, U+FFFD in its place");
}

static void CheckSurrogatePreedit(const Program* program)
{
    size_t at = 0;
    const Record* preedit = Find(program, &at, mwin_eventImePreedit);
    CHECK(preedit != nullptr && preedit->length == 5 &&
              memcmp(preedit->text,
                     "a\xEF\xBF\xBD"
                     "b",
                     5) == 0 &&
              preedit->caret == 5,
          "a composition with a lone surrogate, U+FFFD in its place, the caret after it");
}

static void CheckPlain(const Program* program)
{
    size_t at = 0;
    const Record* preedit = Find(program, &at, mwin_eventImePreedit);
    CHECK(Is(preedit, KA) && preedit->segmentCount == 1 &&
              preedit->segments[0].style == mwin_preeditUnderline && preedit->caret == 3,
          "plain marked text: one clause to convert");
}

// A text past the limit: a reset in its place, no record of its kind,
// and nothing allocated while it was handed over.
static void CheckTooLong(const Program* program, mwinEventKind kind, const char* what)
{
    size_t at = 0;
    const Record* record = Find(program, &at, kind);
    bool none = record == nullptr || (kind == mwin_eventImePreedit && record->length == 0);
    CHECK(none && program->resets == 1 && s_allocations == 0, what);
}

// Hands the view a text past the limit, counting the blocks asked for.
static void HandTooLong(NSView<NSTextInputClient>* view, bool marked)
{
    s_allocations = 0;
    s_counting = true;
    if (marked)
    {
        [view setMarkedText:TOO_LONG
               selectedRange:NSMakeRange(65, 0)
            replacementRange:NSMakeRange(NSNotFound, 0)];
    }
    else
    {
        [view insertText:TOO_LONG replacementRange:NSMakeRange(NSNotFound, 0)];
    }
    s_counting = false;
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
        CHECK(mwinWindow_RequestTextInputCaret(context, program->window,
                                               (mwinRect){10.0f, 20.0f, 2.0f, 16.0f},
                                               nullptr) == mwin_success &&
                  mwinWindow_RequestTextInput(context, program->window, true, nullptr) ==
                      mwin_success,
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
        [view insertText:Surrogate('x', 'y') replacementRange:NSMakeRange(NSNotFound, 0)];
        break;
    case 4:
        CheckSurrogateText(program);
        [view setMarkedText:Surrogate('a', 'b')
               selectedRange:NSMakeRange(3, 0)
            replacementRange:NSMakeRange(NSNotFound, 0)];
        break;
    case 5:
        CheckSurrogatePreedit(program);
        [view setMarkedText:@"か"
               selectedRange:NSMakeRange(1, 0)
            replacementRange:NSMakeRange(NSNotFound, 0)];
        break;
    case 6:
        CheckPlain(program);
        HandTooLong(view, false);
        break;
    case 7:
        CheckTooLong(program, mwin_eventTextInput,
                     "a commit past the limit: a reset, no text, nothing allocated");
        HandTooLong(view, true);
        break;
    case 8:
        CheckTooLong(program, mwin_eventImePreedit,
                     "a composition past the limit: a reset, no composition, nothing allocated");
        [view setMarkedText:@"か"
               selectedRange:NSMakeRange(1, 0)
            replacementRange:NSMakeRange(NSNotFound, 0)];
        break;
    case 9:
        CheckPlain(program);
        CHECK(mwinWindow_RequestTextInput(context, program->window, false, nullptr) == mwin_success,
              "text input off");
        break;
    case 10:
        CheckOff(program, view);
        break;
    default:
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->resets = 0;
    program->startNs = NowNs();
}

// Phases 1 and LAST_PHASE wait for their request's completion.
static bool Pending(const Program* program)
{
    size_t at = 0;
    return (program->phase == 1 || program->phase == LAST_PHASE) &&
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
    return program->phase > LAST_PHASE ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {0};
    setvbuf(stdout, nullptr, _IONBF, 0);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    def.context.allocator = (mwinAllocator){Allocate, Release, nullptr};
    def.context.limits.textBytesPerWindow = TEXT_BYTES;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on macOS");
    CHECK(!program.timedOut, "the window shows in time");
    CHECK(program.phase > LAST_PHASE, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
