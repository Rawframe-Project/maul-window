// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Wayland window's states as the compositor configures them, against
// the test compositor of wayland_server.h and its data device:
// - a window made maximized asks the compositor for it, and has focus
//   once configured activated;
// - a largest size with a free height leaves the height free;
// - a configure that changes the height alone resizes the window;
// - suspended, the window is occluded; neither activated nor suspended,
//   it loses the focus and is revealed;
// - minimized, it stays so through a configure that does not activate
//   it, and is restored by one that does;
// - a maximized mode asked for and configured is done;
// - fullscreen and maximized states together are fullscreen;
// - a maximize still waiting for its configure, superseded by a
//   minimize, is answered once: a clipboard read that takes its slot
//   is answered by its pipe, not by the next configure. Reads of the
//   offered text go first until one finds it, so the client has heard
//   of the offer.
// Skipped (exit status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_data_server.h"

#include "maul-window/clipboard.h"
#include "maul-window/event.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define SETTLE_NS   100000000ull
#define MAX_RECORDS 32
// The caption of the frame the backend draws, which the compositor's
// sizes hold above the content.
#define CAPTION 28

typedef enum Phase
{
    phaseCreate,
    phaseLimits,
    phaseResized,
    phaseOccluded,
    phaseUnfocused,
    phaseMinimized,
    phaseStillMinimized,
    phaseRestored,
    phaseAskMaximized,
    phaseMaximized,
    phaseFullscreen,
    phaseSuperseded,
    phaseOffered,
    phaseForgotten,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    DataServer* data;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinRequestId request;
    mwinRequestId minimize;
    mwinRequestId read;
    // A read that tells whether the client heard of the offer, and when
    // the offer was made.
    mwinRequestId probe;
    uint64_t offeredNs;
    mwinEvent records[MAX_RECORDS];
    int count;
    bool timedOut;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

// The outcome the program read for a request, or -1.
static int OutcomeOf(const Program* program, mwinRequestId request)
{
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* record = &program->records[i];
        if (record->type == mwin_eventRequestCompleted &&
            record->data.completion.request.index1 == request.index1 &&
            record->data.completion.request.generation == request.generation)
        {
            return (int)record->data.completion.outcome;
        }
    }
    return -1;
}

static int CountCompletions(const Program* program)
{
    int completions = 0;
    for (int i = 0; i < program->count; i++)
    {
        completions += program->records[i].type == mwin_eventRequestCompleted;
    }
    return completions;
}

static bool Found(const mwinContext* context, mwinRequestId read, const char* expected,
                  size_t length)
{
    char text[16];
    size_t found = 0;
    return mwinRequest_GetClipboardText(context, read, text, sizeof(text), &found) ==
               mwin_success &&
           found == length && memcmp(text, expected, length) == 0;
}

static const mwinEvent* Find(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

static bool ModeIs(const Program* program, mwinWindowMode mode)
{
    const mwinEvent* changed = Find(program, mwin_eventModeChanged);
    return changed != nullptr && changed->data.mode == mode;
}

static void Configure(const Program* program, int32_t width, int32_t height, const uint32_t* states,
                      int count)
{
    ServerConfigure(program->server, width, height, states, count);
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventWindowCreated) != nullptr;
    case phaseLimits:
        return ServerShell(program->server).maxSize[0] == 300;
    case phaseResized:
        return Find(program, mwin_eventResized) != nullptr;
    case phaseOccluded:
        return Find(program, mwin_eventOccluded) != nullptr;
    case phaseUnfocused:
        return Find(program, mwin_eventFocusLost) != nullptr &&
               Find(program, mwin_eventRevealed) != nullptr;
    case phaseMinimized:
    case phaseRestored:
    case phaseFullscreen:
        return Find(program, mwin_eventModeChanged) != nullptr;
    case phaseForgotten:
        return OutcomeOf(program, program->read) >= 0 &&
               Find(program, mwin_eventModeChanged) != nullptr;
    case phaseSuperseded:
        return CountCompletions(program) >= 2 && Find(program, mwin_eventModeChanged) != nullptr;
    case phaseStillMinimized:
        return NowNs() - program->startNs >= SETTLE_NS;
    case phaseOffered:
        return OutcomeOf(program, program->probe) >= 0;
    case phaseAskMaximized:
        return ServerShell(program->server).maximizes >= 2;
    case phaseMaximized:
        return Find(program, mwin_eventRequestCompleted) != nullptr;
    default:
        return false;
    }
}

static void Probe(Program* program, mwinContext* context)
{
    CHECK(mwinWindow_RequestClipboardRead(context, program->window, &program->probe) ==
              mwin_success,
          "a read of the offer asked for");
}

static void Advance(Program* program, mwinContext* context)
{
    static const uint32_t s_activated[] = {XDG_TOPLEVEL_STATE_ACTIVATED};
    static const uint32_t s_suspended[] = {XDG_TOPLEVEL_STATE_ACTIVATED,
                                           XDG_TOPLEVEL_STATE_SUSPENDED};
    static const uint32_t s_maximized[] = {XDG_TOPLEVEL_STATE_MAXIMIZED,
                                           XDG_TOPLEVEL_STATE_ACTIVATED};
    static const uint32_t s_both[] = {XDG_TOPLEVEL_STATE_FULLSCREEN, XDG_TOPLEVEL_STATE_MAXIMIZED,
                                      XDG_TOPLEVEL_STATE_ACTIVATED};
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(ServerShell(program->server).maximizes == 1,
              "a window made maximized asks the compositor for it");
        CHECK(Find(program, mwin_eventFocusGained) != nullptr,
              "configured activated, it has focus");
        CHECK(mwinWindow_RequestSizeLimits(context, program->window, (mwinSize){0.0f, 0.0f},
                                           (mwinSize){300.0f, 0.0f}, nullptr) == mwin_success,
              "a largest width");
        break;
    case phaseLimits:
        CHECK(ServerShell(program->server).maxSize[1] == 0, "a free height left free");
        Configure(program, 640, 400 + CAPTION, s_activated, 1);
        break;
    case phaseResized:
    {
        const mwinEvent* resized = Find(program, mwin_eventResized);
        CHECK(resized->data.size.width == 640.0f && resized->data.size.height == 400.0f,
              "a configure that changes the height alone resizes the window");
        Configure(program, 0, 0, s_suspended, 2);
        break;
    }
    case phaseOccluded:
        Configure(program, 0, 0, nullptr, 0);
        break;
    case phaseUnfocused:
        CHECK(mwinWindow_RequestMode(context, program->window, mwin_modeMinimized, nullptr) ==
                  mwin_success,
              "minimize");
        break;
    case phaseMinimized:
        CHECK(ModeIs(program, mwin_modeMinimized), "minimized");
        Configure(program, 0, 0, nullptr, 0);
        break;
    case phaseStillMinimized:
        CHECK(Find(program, mwin_eventModeChanged) == nullptr,
              "a configure that does not activate it leaves it minimized");
        Configure(program, 0, 0, s_activated, 1);
        break;
    case phaseRestored:
        CHECK(ModeIs(program, mwin_modeWindowed), "one that activates it restores it");
        CHECK(mwinWindow_RequestMode(context, program->window, mwin_modeMaximized,
                                     &program->request) == mwin_success,
              "maximize");
        break;
    case phaseAskMaximized:
        Configure(program, 0, 0, s_maximized, 2);
        break;
    case phaseMaximized:
        CHECK(Find(program, mwin_eventRequestCompleted)->data.completion.outcome ==
                      mwin_outcomeDone &&
                  ModeIs(program, mwin_modeMaximized),
              "a maximized mode asked for and configured is done");
        Configure(program, 0, 0, s_both, 3);
        break;
    case phaseFullscreen:
        CHECK(ModeIs(program, mwin_modeBorderlessFullscreen),
              "fullscreen and maximized states together are fullscreen");
        // A maximize waits for its configure; the minimize after it
        // supersedes it and is done at once.
        CHECK(mwinWindow_RequestMode(context, program->window, mwin_modeMaximized,
                                     &program->request) == mwin_success &&
                  mwinWindow_RequestMode(context, program->window, mwin_modeMinimized,
                                         &program->minimize) == mwin_success,
              "maximize, then minimize before the configure");
        break;
    case phaseSuperseded:
        CHECK(OutcomeOf(program, program->request) == mwin_outcomeSuperseded &&
                  OutcomeOf(program, program->minimize) == mwin_outcomeDone &&
                  ModeIs(program, mwin_modeMinimized),
              "the maximize superseded, the minimize done");
        // Another client's selection, which the client hears of first.
        DataOffer(program->data, "paste", 5);
        program->offeredNs = NowNs();
        Probe(program, context);
        break;
    case phaseOffered:
        CHECK(OutcomeOf(program, program->probe) == mwin_outcomeDone, "a read of the offer");
        if (!Found(context, program->probe, "paste", 5) &&
            NowNs() - program->offeredNs < DEADLINE_NS)
        {
            // Read as empty: the offer has not reached the client yet.
            Probe(program, context);
            program->phase = phaseSuperseded;
            break;
        }
        CHECK(Found(context, program->probe, "paste", 5), "the client hears of the offer");
        // The read takes the lowest free slot, the maximize's, and waits
        // for its pipe; the configure comes before any of its bytes.
        CHECK(mwinWindow_RequestClipboardRead(context, program->window, &program->read) ==
                      mwin_success &&
                  program->read.index1 == program->request.index1,
              "a clipboard read in the maximize's slot");
        Configure(program, 0, 0, s_activated, 1);
        break;
    default:
        CHECK(OutcomeOf(program, program->read) == mwin_outcomeDone &&
                  Found(context, program->read, "paste", 5) && CountCompletions(program) == 1,
              "the configure answers nothing: the read is done with the offered text");
        CHECK(ModeIs(program, mwin_modeWindowed), "the configure restores the window");
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){640.0f, 480.0f};
    def.mode = mwin_modeMaximized;
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 500000};
        // hygiene: yields between frames; the wait ends on a report, within the deadline
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    static Server server;
    static DataServer data;
    if (runtime == nullptr || runtime[0] == '\0')
    {
        // hygiene: no Wayland display here is a skip, and CI requires one
        return 77;
    }
    if (!ServerStart(&server, "us", ""))
    {
        printf("FAIL: the stand-in compositor did not start\n");
        return 1;
    }
    DataStart(&data, &server);
    // The data server writes as another client would: a read the program
    // dropped costs that writer EPIPE, and the checks say what went wrong.
    (void)signal(SIGPIPE, SIG_IGN);
    static Program program;
    program = (Program){.server = &server, .data = &data};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    DataStop(&data);
    return s_failures == 0 ? 0 : 1;
}
