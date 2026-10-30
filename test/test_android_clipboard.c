// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard on Android, in the emulator (tools/run_android_app.sh):
// text written to the clipboard reads back the same, as do ASCII at the
// limit and empty text, each read while the program has the focus. On a
// failed check it prints the phase's completions, each request's kind
// and outcome.

#include "test_harness.h"

#include "maul-window/clipboard.h"
#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_window.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define LAST_PHASE  3

// Text past ASCII, in one, two and three bytes.
#define TEXT "H\xc3\xa9llo, \xe2\x9c\x93 w\xc3\xb6rld"
// ASCII as long as TEXT in bytes, the clipboard's limit: as many units.
#define LIMIT_TEXT "maul-window-limits"
static_assert(sizeof(LIMIT_TEXT) == sizeof(TEXT), "the limit is TEXT's bytes");

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    int completions;
    mwinOutcome outcomes[4];
    mwinRequestKind kinds[4];
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventRequestCompleted && program->completions < 4)
        {
            program->kinds[program->completions] = event.data.completion.kind;
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
    }
}

static mwinWindowState StateOf(const Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinWindow_GetState(context, program->window, &state);
    return state;
}

// Shows a frame, drawn on the CPU: Android 14 and later send nothing to
// a window whose surface never showed one.
static void Paint(const Program* program, mwinContext* context)
{
    mwinNativeHandles handles = {0};
    if (mwinWindow_GetNativeHandles(context, program->window, &handles) != mwin_success)
    {
        return;
    }
    ANativeWindow* window = handles.handles.android.window;
    ANativeWindow_Buffer buffer;
    (void)ANativeWindow_setBuffersGeometry(window, 0, 0, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    if (ANativeWindow_lock(window, &buffer, nullptr) == 0)
    {
        for (int32_t row = 0; row < buffer.height; row++)
        {
            memset((uint8_t*)buffer.bits + (size_t)row * (size_t)buffer.stride * 4u, 0x40,
                   (size_t)buffer.width * 4u);
        }
        (void)ANativeWindow_unlockAndPost(window);
    }
}

// Each phase waits for the focus, which Android asks of a read, and for
// the write and the read asked before it.
static bool Ready(const Program* program, mwinContext* context)
{
    return StateOf(program, context).focused && (program->phase == 0 || program->completions >= 2);
}

// The last clipboard read asked, whose payload the checks copy out.
static mwinRequestId s_read;

static bool ReadBack(mwinContext* context, const char* expected)
{
    char text[64] = {0};
    size_t length = 0;
    return mwinRequest_GetClipboardText(context, s_read, text, sizeof(text), &length) ==
               mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

// Writes text and reads it back: two requests.
static void WriteRead(const Program* program, mwinContext* context, const char* text)
{
    CHECK(mwinWindow_RequestClipboardWrite(context, program->window, text, strlen(text), nullptr) ==
                  mwin_success &&
              mwinWindow_RequestClipboardRead(context, program->window, &s_read) == mwin_success,
          "a write and a read asked for");
}

// Prints a phase's completions in the order they came, each its request's
// kind and its outcome, so that a failed check shows what it read.
static void PrintCompletions(const Program* program)
{
    printf("phase %d's %d completions (kind:outcome):", program->phase, program->completions);
    for (int i = 0; i < program->completions; i++)
    {
        printf(" %d:%d", (int)program->kinds[i], (int)program->outcomes[i]);
    }
    printf("\n");
}

static void Advance(Program* program, mwinContext* context)
{
    int failures = s_failures;
    const mwinOutcome* outcomes = program->outcomes;
    switch (program->phase)
    {
    case 0:
        Paint(program, context);
        WriteRead(program, context, TEXT);
        break;
    case 1:
        CHECK(outcomes[0] == mwin_outcomeDone && outcomes[1] == mwin_outcomeDone &&
                  ReadBack(context, TEXT),
              "the clipboard's text read back the same");
        WriteRead(program, context, LIMIT_TEXT);
        break;
    case 2:
        CHECK(outcomes[0] == mwin_outcomeDone && outcomes[1] == mwin_outcomeDone &&
                  ReadBack(context, LIMIT_TEXT),
              "ASCII at the limit read back the same");
        WriteRead(program, context, "");
        break;
    case 3:
        CHECK(outcomes[0] == mwin_outcomeDone && outcomes[1] == mwin_outcomeDone &&
                  ReadBack(context, ""),
              "empty text read back empty");
        break;
    default:
        break;
    }
    if (s_failures > failures)
    {
        PrintCompletions(program);
    }
    program->phase += 1;
    program->completions = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (program->phase > LAST_PHASE)
    {
        return mwin_frameStop;
    }
    if (Ready(program, context))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d\n", program->phase);
        PrintCompletions(program);
        s_failures += 1;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase > LAST_PHASE, "every phase ran");
    printf("result: %d failures\n", s_failures);
}

mwinAppDef mwinAndroidMain(void)
{
    static Program program;
    // The runner reads the file with run-as.
    if (freopen(OUT_PATH, "w", stdout) != nullptr)
    {
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.clipboardBytes = sizeof(LIMIT_TEXT) - 1;
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    return def;
}
