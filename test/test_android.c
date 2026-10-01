// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android backend against the platform, in the emulator
// (tools/run_android_app.sh): the program asked for once; its window
// made at the activity's first surface, with the native window and the
// activity as its handles, its pixel size the native window's and its
// scale the density's; a second window unsupported, showing done, a size
// request unsupported. Then what only the system does, asked of the
// runner: the home key, the application suspending in a frame of its own
// and its surface lost in another, no frames while suspended (the
// suspended record giving way to the resuming one); brought back,
// resuming with a new surface; turned, the activity made anew, which
// joins the running program: the window goes on with a new surface and
// the program is not asked for again. quit writes the closing line the
// runner waits for.

#include "test_harness.h"

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
#define START       "adb: am start -n " MWIN_TEST_PACKAGE "/maul.window.Activity\n"
#define RECORDS     32

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinWindowId second;
    bool created;
    bool shown;
    mwinOutcome secondOutcome;
    mwinOutcome visibleOutcome;
    mwinOutcome sizeOutcome;
    int completions;
    // The lifecycle and surface records since the phase began, in order,
    // and whether a frame with none ran while the program was suspended
    // (the critical frames are the ones that carry them).
    mwinEventType records[RECORDS];
    int recordCount;
    bool suspended;
    bool framedSuspended;
    uint32_t generation;
    // The frame being run, and those that first told of the surface lost,
    // the application suspending and resuming.
    int frame;
    int lostFrame;
    int suspendingFrame;
    int resumingFrame;
    // Whether the window was taller than wide before it was turned.
    bool wasPortrait;
} Program;

static int s_mains;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static bool SameWindow(mwinWindowId a, mwinWindowId b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static bool IsRecord(mwinEventType type)
{
    return (type >= mwin_eventSuspending && type <= mwin_eventSurfaceRestored) ||
           type == mwin_eventWindowCreated;
}

static void Complete(Program* program, const mwinEvent* event)
{
    const mwinCompletion* completion = &event->data.completion;
    program->completions += 1;
    if (SameWindow(event->window, program->second))
    {
        program->secondOutcome = completion->outcome;
    }
    else if (completion->kind == mwin_requestVisible)
    {
        program->visibleOutcome = completion->outcome;
    }
    else if (completion->kind == mwin_requestSize)
    {
        program->sizeOutcome = completion->outcome;
    }
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (IsRecord(event.type) && program->recordCount < RECORDS)
        {
            program->records[program->recordCount++] = event.type;
        }
        int* first = event.type == mwin_eventSurfaceLost  ? &program->lostFrame
                     : event.type == mwin_eventSuspending ? &program->suspendingFrame
                     : event.type == mwin_eventResuming   ? &program->resumingFrame
                                                          : nullptr;
        if (first != nullptr && *first == 0)
        {
            *first = program->frame;
        }
        switch (event.type)
        {
        case mwin_eventWindowCreated:
            program->created |= SameWindow(event.window, program->window);
            break;
        case mwin_eventShown:
            program->shown = true;
            break;
        case mwin_eventSuspending:
            // The program asks the runner to bring it back, in the frame
            // that tells it, the last before it is suspended.
            if (program->phase == 2)
            {
                printf(START);
            }
            break;
        case mwin_eventSuspended:
            program->suspended = true;
            break;
        case mwin_eventResuming:
            program->suspended = false;
            break;
        case mwin_eventRequestCompleted:
            Complete(program, &event);
            break;
        default:
            break;
        }
    }
}

static bool Saw(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->recordCount; i++)
    {
        if (program->records[i] == type)
        {
            return true;
        }
    }
    return false;
}

static bool SawInOrder(const Program* program, const mwinEventType* order, int count)
{
    int next = 0;
    for (int i = 0; i < program->recordCount && next < count; i++)
    {
        next += program->records[i] == order[next];
    }
    return next == count;
}

static mwinWindowState StateOf(const Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinGetWindowState(context, program->window, &state);
    return state;
}

// The window as made: its handles, and its sizes from them.
static void CheckWindow(const Program* program, mwinContext* context)
{
    CHECK(s_mains == 1, "the program asked for once");
    mwinNativeHandles handles = {0};
    CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success &&
              handles.platform == mwin_platformAndroid && handles.handles.android.window &&
              handles.handles.android.activity,
          "the native window and the activity");
    mwinWindowState state = StateOf(program, context);
    ANativeWindow* window = handles.handles.android.window;
    CHECK(window != nullptr && (int32_t)state.pixelSize.width == ANativeWindow_getWidth(window) &&
              (int32_t)state.pixelSize.height == ANativeWindow_getHeight(window),
          "the native window's size in pixels");
    CHECK(state.scale >= 1.0f && state.size.width * state.scale > state.pixelSize.width - 1.0f &&
              state.size.width * state.scale < state.pixelSize.width + 1.0f,
          "the scale, and the size in logical units");
    CHECK(program->shown && state.visible, "shown with the activity");
}

static bool Ready(const Program* program, mwinContext* context)
{
    // A suspended record still waiting when the application resumes
    // gives way to the resuming one (no frames run between them).
    static const mwinEventType order[] = {mwin_eventSuspending, mwin_eventResuming,
                                          mwin_eventResumed};
    switch (program->phase)
    {
    case 0:
        return program->created && program->shown;
    case 1:
        return program->completions >= 3;
    case 2:
    case 3:
    {
        // Away and back, the surface lost and restored; turned, at last.
        mwinWindowState state = StateOf(program, context);
        return SawInOrder(program, order, 3) && Saw(program, mwin_eventSurfaceLost) &&
               Saw(program, mwin_eventSurfaceRestored) && !state.surfaceLost &&
               (program->phase == 2 ||
                (state.pixelSize.width > state.pixelSize.height) == program->wasPortrait);
    }
    default:
        return true;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    mwinWindowState state = StateOf(program, context);
    switch (program->phase)
    {
    case 0:
    {
        CheckWindow(program, context);
        program->generation = state.surfaceGeneration;
        mwinWindowDef def = mwinDefaultWindowDef();
        CHECK(mwinCreateWindow(context, &def, &program->second, nullptr) == mwin_success,
              "a second window asked for");
        CHECK(mwinRequestVisible(context, program->window, true, nullptr) == mwin_success &&
                  mwinRequestSize(context, program->window, (mwinSize){100, 100}, nullptr) ==
                      mwin_success,
              "showing and sizing asked for");
        break;
    }
    case 1:
        CHECK(program->secondOutcome == mwin_outcomeUnsupported, "one window");
        CHECK(program->visibleOutcome == mwin_outcomeDone, "showing done");
        CHECK(program->sizeOutcome == mwin_outcomeUnsupported, "the system sizes it");
        printf("adb: input keyevent KEYCODE_HOME\n");
        break;
    case 2:
        CHECK(!program->framedSuspended, "no frames while suspended");
        // Android waits for the surface to go: the program hears of it in
        // a frame of its own, whichever comes first, it or the stop.
        CHECK(program->lostFrame != program->suspendingFrame &&
                  program->lostFrame != program->resumingFrame,
              "the lost surface told in a frame of its own");
        CHECK(state.surfaceGeneration == program->generation + 1, "a new surface");
        program->generation = state.surfaceGeneration;
        // Turned the other way: the activity is made anew (no
        // configChanges).
        program->wasPortrait = state.pixelSize.height > state.pixelSize.width;
        printf("adb: settings put system accelerometer_rotation 0\n");
        printf("adb: settings put system user_rotation %d\n", program->wasPortrait ? 1 : 0);
        break;
    case 3:
        CHECK(s_mains == 1, "the activity made anew joined the program");
        CHECK(state.surfaceGeneration > program->generation, "the new activity's surface");
        CHECK((state.pixelSize.width > state.pixelSize.height) == program->wasPortrait, "turned");
        CheckWindow(program, context);
        break;
    default:
        break;
    }
    program->phase += 1;
    program->recordCount = 0;
    program->lostFrame = 0;
    program->suspendingFrame = 0;
    program->resumingFrame = 0;
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
    bool suspended = program->suspended;
    int records = program->recordCount;
    program->frame += 1;
    Collect(program, context);
    program->framedSuspended |= suspended && program->recordCount == records;
    if (program->phase == 4)
    {
        return mwin_frameStop;
    }
    if (Ready(program, context))
    {
        printf("phase %d\n", program->phase);
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d, after records", program->phase);
        for (int i = 0; i < program->recordCount; i++)
        {
            printf(" %d", (int)program->records[i]);
        }
        printf(", surface lost %d\n", (int)StateOf(program, context).surfaceLost);
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
    CHECK(program->phase == 4, "every phase ran");
    printf("result: %d failures\n", s_failures);
}

mwinAppDef mwinAndroidMain(void)
{
    static Program program;
    s_mains += 1;
    // The runner reads the file with run-as.
    if (freopen(OUT_PATH, "w", stdout) != nullptr)
    {
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    return def;
}
