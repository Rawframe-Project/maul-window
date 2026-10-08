// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's preferred locales in headless Chrome, as the page's
// languages change: every language of the list, in order; a list a byte
// shorter than the backend's bytes taken whole; and a list of the
// backend's bytes left out, the locales kept as they were.

#include "test_harness.h"
#include "web_js.h"

#include "maul-window/event.h"
#include "maul-window/system.h"

#include <string.h>

#define DEADLINE_MS 10000.0
#define MAX_RECORDS 32
#define LIST_BYTES  512

typedef enum Phase
{
    phaseList,
    phaseLongest,
    phaseTooLong,
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
// Gives the page the languages, a comma between each, as a browser
// would when its user changes them.
EM_JS(void, SetLanguages, (const char* list, size_t length), {
    const languages = UTF8ToString(list, length).split(',');
    Object.defineProperty(navigator, 'languages', {configurable: true, get: () => languages});
    window.dispatchEvent(new Event('languagechange'));
});
// clang-format on

static char s_list[LIST_BYTES];

// A list of one language of the bytes given.
static void SetLanguageOf(size_t length)
{
    memset(s_list, 'x', length);
    SetLanguages(s_list, length);
}

static bool Changed(const Program* program)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == mwin_eventLocaleChanged)
        {
            return true;
        }
    }
    return false;
}

static bool LocalesAre(const mwinContext* context, const char* expected, size_t length)
{
    static char locales[LIST_BYTES];
    size_t got = 0;
    return mwinGetPreferredLocales(context, locales, sizeof(locales), &got) == mwin_success &&
           got == length && memcmp(locales, expected, length) == 0;
}

static bool Ready(Program* program)
{
    if (program->phase == phaseTooLong)
    {
        // Nothing comes: a few frames show it.
        return ++program->frames > 10;
    }
    return Changed(program);
}

static void Advance(Program* program, mwinContext* context)
{
    static const char languages[] = "tr-TR,en-US";
    switch (program->phase)
    {
    case phaseList:
        CHECK(LocalesAre(context, languages, sizeof(languages) - 1), "every language, in order");
        SetLanguageOf(LIST_BYTES - 1);
        break;
    case phaseLongest:
        CHECK(LocalesAre(context, s_list, LIST_BYTES - 1), "a list a byte shorter taken whole");
        SetLanguageOf(LIST_BYTES);
        break;
    default:
        CHECK(!Changed(program) && LocalesAre(context, s_list, LIST_BYTES - 1),
              "a list of the backend's bytes left out");
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startMs = mwinWebNow();
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    Program* program = user;
    program->startMs = mwinWebNow();
    SetLanguages("tr-TR,en-US", 11);
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
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    // Room for every list the backend takes.
    def.context.limits.localeBytes = LIST_BYTES;
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
