// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The macOS backend's clipboard and services, against macOS's own (a CI
// runner's session): text written to the pasteboard read back whole;
// the display kept awake while a window asks, read back from the
// process's power assertions, and let go when it no longer asks; a file
// that does not exist not revealed, and one that does shown in the
// Finder. Opening an address is left out: it would start the runner's
// browser, and nothing here could tell that it did.

#include "test_harness.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"
#include "maul-window/services.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 10000000000ull
// "Maul é", past ASCII.
#define TEXT "Maul \xC3\xA9"

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    int completions;
    mwinOutcome outcomes[4];
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// Whether this process holds an assertion that keeps the display awake.
static bool Awake(void)
{
    CFDictionaryRef byProcess = nullptr;
    if (IOPMCopyAssertionsByProcess(&byProcess) != kIOReturnSuccess || byProcess == nullptr)
    {
        return false;
    }
    int pid = getpid();
    CFNumberRef key = CFNumberCreate(nullptr, kCFNumberIntType, &pid);
    CFArrayRef mine = CFDictionaryGetValue(byProcess, key);
    bool found = false;
    for (CFIndex i = 0; mine != nullptr && i < CFArrayGetCount(mine); i++)
    {
        CFDictionaryRef assertion = CFArrayGetValueAtIndex(mine, i);
        CFStringRef type = CFDictionaryGetValue(assertion, kIOPMAssertionTypeKey);
        found |= type != nullptr && CFEqual(type, kIOPMAssertPreventUserIdleDisplaySleep);
    }
    CFRelease(key);
    CFRelease(byProcess);
    return found;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        if (event.type == mwin_eventRequestCompleted && program->completions < 4)
        {
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
    }
}

// Each phase asks, and the next reads the answers once they came.
static void Advance(Program* program, mwinContext* context)
{
    mwinWindowId window = program->window;
    switch (program->phase)
    {
    case 0:
        CHECK(mwinRequestClipboardWrite(context, window, TEXT, sizeof(TEXT) - 1, nullptr) ==
                      mwin_success &&
                  mwinRequestClipboardRead(context, window, nullptr) == mwin_success &&
                  mwinRequestKeepAwake(context, window, true, nullptr) == mwin_success,
              "write, read, keep awake");
        break;
    case 1:
    {
        char text[32];
        size_t length = 0;
        CHECK(program->outcomes[0] == mwin_outcomeDone &&
                  program->outcomes[1] == mwin_outcomeDone &&
                  mwinGetClipboardText(context, text, sizeof(text), &length) == mwin_success &&
                  length == sizeof(TEXT) - 1 && memcmp(text, TEXT, length) == 0,
              "the text written is read back whole");
        CHECK(program->outcomes[2] == mwin_outcomeDone && Awake(),
              "the display kept awake while the window asks");
        static const char missing[] = "/nonexistent/maul-window";
        static const char present[] = "/System";
        CHECK(mwinRequestKeepAwake(context, window, false, nullptr) == mwin_success &&
                  mwinRequestRevealFile(context, window, missing, sizeof(missing) - 1, nullptr) ==
                      mwin_success &&
                  mwinRequestRevealFile(context, window, present, sizeof(present) - 1, nullptr) ==
                      mwin_success,
              "let go, reveal");
        break;
    }
    case 2:
        CHECK(program->outcomes[0] == mwin_outcomeDone && !Awake(),
              "let go when it no longer asks");
        CHECK(program->outcomes[1] == mwin_outcomeFailed, "a file that does not exist");
        CHECK(program->outcomes[2] == mwin_outcomeDone, "one that does, shown");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->completions = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul macOS services";
    def.titleLength = 19;
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    // The awake state follows at the pump after a completion, so each
    // phase waits a frame past its answers.
    static const int answers[] = {0, 3, 3};
    bool ready = program->shown && program->completions >= answers[program->phase % 3] &&
                 NowNs() - program->startNs > 50000000u;
    if (ready)
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    return program->phase == 3 ? mwin_frameStop : mwin_frameContinue;
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
    CHECK(!program.timedOut, "every answer comes in time");
    CHECK(program.phase == 3, "every phase ran");
    return s_failures == 0 ? 0 : 1;
}
