// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android backend against the platform, in the emulator
// (tools/run_android_app.sh): the program asked for once; its window
// made at the activity's first surface, its request done, with the
// native window and the activity as its handles, its pixel size the
// native window's and its scale the density's; the display its one
// monitor; a second window unsupported, showing done, a size request
// unsupported. Then what only the system does, asked of the runner: the
// home key, the application suspending and the window occluded in a
// frame of their own and its surface lost in another, no frames while
// suspended (the suspended record giving way to the resuming one);
// brought back, resuming with a new surface, and the window revealed
// after; turned, the activity made anew, which joins the running
// program: the window goes on with a new surface, not asked to close, and
// the program is not asked for again; finished, the activity asks the
// window to close, which the program hears once a new activity joins.
// Then the window destroyed, its id gone, and a new window made, which
// that activity shows. Each phase that checks the window waits for the
// display's facts to agree with it, less than a second. quit writes the
// closing line the runner waits for.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define START       "adb: am start -n " MWIN_TEST_PACKAGE "/maul.window.Activity\n"
#define RECORDS     32
#define LAST_PHASE  7

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinWindowId second;
    bool created;
    bool shown;
    bool createDone;
    mwinOutcome secondOutcome;
    mwinOutcome visibleOutcome;
    mwinOutcome sizeOutcome;
    int completions;
    int closeRequests;
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
    // When the window was ready for a phase's checks while its monitor's
    // facts did not yet agree with it, or 0.
    uint64_t disagreedNs;
    // The frame that first told the window occluded, and whether its
    // state said so then.
    int occludedFrame;
    bool occludedState;
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
           type == mwin_eventWindowCreated || type == mwin_eventWindowDestroyed ||
           type == mwin_eventOccluded || type == mwin_eventRevealed;
}

static void Complete(Program* program, const mwinEvent* event)
{
    const mwinCompletion* completion = &event->data.completion;
    program->completions += 1;
    if (SameWindow(event->window, program->second))
    {
        program->secondOutcome = completion->outcome;
    }
    else if (completion->kind == mwin_requestCreate)
    {
        program->createDone = completion->outcome == mwin_outcomeDone;
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
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        if (IsRecord(event.type) && program->recordCount < RECORDS)
        {
            program->records[program->recordCount++] = event.type;
        }
        int* first = event.type == mwin_eventSurfaceLost  ? &program->lostFrame
                     : event.type == mwin_eventSuspending ? &program->suspendingFrame
                     : event.type == mwin_eventResuming   ? &program->resumingFrame
                     : event.type == mwin_eventOccluded   ? &program->occludedFrame
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
        case mwin_eventCloseRequested:
            program->closeRequests += 1;
            break;
        case mwin_eventOccluded:
        {
            mwinWindowState state = {0};
            program->occludedState =
                mwinWindow_GetState(context, program->window, &state) == mwin_success &&
                state.occluded;
            break;
        }
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

// The window's monitor, the display, and its facts; false without them.
static bool MonitorOf(mwinContext* context, mwinMonitorId* monitor, mwinMonitorInfo* info)
{
    size_t count = 0;
    return mwinContext_GetMonitors(context, monitor, 1, &count) == mwin_success && count == 1 &&
           mwinMonitor_GetInfo(context, *monitor, info) == mwin_success;
}

// Whether the monitor's facts agree with the window: the display at
// least as large, each way, and the same scale.
static bool Agrees(const mwinMonitorInfo* info, const mwinWindowState* state)
{
    return info->scale == state->scale && info->bounds.width >= state->pixelSize.width &&
           info->bounds.height >= state->pixelSize.height;
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
    (void)mwinWindow_GetState(context, program->window, &state);
    return state;
}

// The display as the one monitor, the window's, turned as the window is;
// the emulator shows no HDR.
static void CheckMonitor(const mwinWindowState* state, mwinContext* context)
{
    mwinMonitorId monitor = {0};
    mwinMonitorInfo info = {0};
    CHECK(MonitorOf(context, &monitor, &info) &&
              memcmp(&monitor, &state->monitor, sizeof(monitor)) == 0,
          "one monitor, the window's");
    bool facts = info.primary && info.nameLength > 0 && Agrees(&info, state) && info.widthMm > 0 &&
                 info.heightMm > 0 && info.refreshMilliHz > 0;
    CHECK(facts, "the display's facts, turned as the window");
    if (!facts)
    {
        printf("  the display %ux%u pixels at scale %.3f, %ux%u mm, %u mHz, named %d bytes; the "
               "window %ux%u pixels at scale %.3f\n",
               info.bounds.width, info.bounds.height, (double)info.scale, info.widthMm,
               info.heightMm, info.refreshMilliHz, (int)info.nameLength, state->pixelSize.width,
               state->pixelSize.height, (double)state->scale);
    }
    CHECK(info.hdr.known && !info.hdr.active && info.hdr.headroom == 1.0f &&
              info.hdr.sdrWhiteNits == 0.0f && info.variableRefresh == mwin_unknown,
          "no HDR on the emulator's display, variable refresh unknown before Android 16");
}

// Android's own scale for the activity: its display metrics' density.
static float DensityOf(ANativeActivity* activity)
{
    JNIEnv* env = activity->env;
    jclass activities = (*env)->GetObjectClass(env, activity->clazz);
    jobject resources = (*env)->CallObjectMethod(
        env, activity->clazz,
        (*env)->GetMethodID(env, activities, "getResources", "()Landroid/content/res/Resources;"));
    jclass resourceTypes = (*env)->GetObjectClass(env, resources);
    jobject metrics =
        (*env)->CallObjectMethod(env, resources,
                                 (*env)->GetMethodID(env, resourceTypes, "getDisplayMetrics",
                                                     "()Landroid/util/DisplayMetrics;"));
    jclass metricTypes = (*env)->GetObjectClass(env, metrics);
    float density =
        (*env)->GetFloatField(env, metrics, (*env)->GetFieldID(env, metricTypes, "density", "F"));
    (*env)->ExceptionClear(env);
    jobject locals[] = {activities, resources, resourceTypes, metrics, metricTypes};
    for (size_t i = 0; i < sizeof(locals) / sizeof(locals[0]); i++)
    {
        (*env)->DeleteLocalRef(env, locals[i]);
    }
    return density;
}

// The window as made: its handles, and its sizes from them.
static void CheckWindow(const Program* program, mwinContext* context)
{
    CHECK(s_mains == 1, "the program asked for once");
    mwinNativeHandles handles = {0};
    CHECK(mwinWindow_GetNativeHandles(context, program->window, &handles) == mwin_success &&
              handles.platform == mwin_platformAndroid && handles.handles.android.window &&
              handles.handles.android.activity,
          "the native window and the activity");
    mwinWindowState state = StateOf(program, context);
    ANativeWindow* window = handles.handles.android.window;
    CHECK(window != nullptr && (int32_t)state.pixelSize.width == ANativeWindow_getWidth(window) &&
              (int32_t)state.pixelSize.height == ANativeWindow_getHeight(window),
          "the native window's size in pixels");
    // A missing activity is the failure above, not a crash here.
    ANativeActivity* activity = handles.handles.android.activity;
    CHECK(activity != nullptr && fabsf(state.scale - DensityOf(activity)) < 0.01f &&
              state.size.width * state.scale > state.pixelSize.width - 1.0f &&
              state.size.width * state.scale < state.pixelSize.width + 1.0f,
          "the scale the display's density, and the size in logical units");
    CHECK(program->shown && state.visible, "shown with the activity");
    CheckMonitor(&state, context);
}

// Whether the window has its surface and the activity's handles. A
// frame between a relaunched activity's start and its surface, which
// the start itself runs, gets no handles: the library answers
// mwin_errorState and gives out no pointer, so a program that tests the
// status never reads one.
static bool HasSurface(const Program* program, mwinContext* context)
{
    mwinNativeHandles handles;
    memset(&handles, 0xA5, sizeof(handles));
    mwinResult status = mwinWindow_GetNativeHandles(context, program->window, &handles);
    if (StateOf(program, context).surfaceLost)
    {
        static const mwinNativeHandles none = {0};
        CHECK(status == mwin_errorState && memcmp(&handles, &none, sizeof(handles)) == 0,
              "no handles while the surface is gone");
        return false;
    }
    return status == mwin_success && handles.handles.android.window != nullptr &&
           handles.handles.android.activity != nullptr;
}

static bool Ready(const Program* program, mwinContext* context)
{
    // A suspended record still waiting when the application resumes
    // gives way to the resuming one (no frames run between them). The
    // window is occluded as the activity stops, after the suspending
    // record (lifecycle records come first), and revealed once it
    // started again.
    static const mwinEventType order[] = {mwin_eventSuspending, mwin_eventOccluded,
                                          mwin_eventResuming, mwin_eventResumed,
                                          mwin_eventRevealed};
    switch (program->phase)
    {
    case 0:
    case 6:
        // Shown, and showing now: a turn that relaunches the activity
        // takes the surface away for a while, and its new start runs a
        // frame before the surface comes back.
        return program->created && program->shown && HasSurface(program, context);
    case 1:
        return program->completions >= 3;
    case 4:
        return program->closeRequests > 0;
    case 5:
        return Saw(program, mwin_eventWindowDestroyed);
    case 2:
    case 3:
    {
        // Away and back, the surface lost and restored; turned, at last.
        mwinWindowState state = StateOf(program, context);
        return SawInOrder(program, order, 5) && Saw(program, mwin_eventSurfaceLost) &&
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
        CHECK(program->createDone, "the window's making done as it shows");
        program->generation = state.surfaceGeneration;
        mwinWindowDef def = mwinDefaultWindowDef();
        CHECK(mwinCreateWindow(context, &def, &program->second, nullptr) == mwin_success,
              "a second window asked for");
        CHECK(mwinWindow_RequestVisible(context, program->window, true, nullptr) == mwin_success &&
                  mwinWindow_RequestSize(context, program->window, (mwinSize){100, 100}, nullptr) ==
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
        CHECK(program->occludedFrame == program->suspendingFrame && program->occludedState &&
                  !state.occluded,
              "occluded in the frame that tells of the suspending, revealed once it runs again");
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
        CHECK(program->closeRequests == 0, "made anew, the window not asked to close");
        {
            // The activity finished, as Back at the root does it; the
            // program, with no activity, runs no frames until one joins.
            mwinNativeHandles handles = {0};
            (void)mwinWindow_GetNativeHandles(context, program->window, &handles);
            ANativeActivity_finish(handles.handles.android.activity);
            printf(START);
        }
        break;
    case 4:
        CHECK(program->closeRequests == 1, "the activity finished: the window asked to close");
        // Closed as asked; then a new window, which the activity that
        // joined shows.
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "the window destroyed");
        break;
    case 5:
    {
        mwinWindowState gone;
        CHECK(mwinWindow_GetState(context, program->window, &gone) == mwin_errorStale,
              "the destroyed window gone");
        mwinWindowDef def = mwinDefaultWindowDef();
        program->created = false;
        program->shown = false;
        program->createDone = false;
        CHECK(mwinCreateWindow(context, &def, &program->window, nullptr) == mwin_success,
              "a new window asked for");
        break;
    }
    case 6:
        CheckWindow(program, context);
        CHECK(program->createDone, "the new window's making done as it shows");
        break;
    default:
        break;
    }
    program->phase += 1;
    program->recordCount = 0;
    program->lostFrame = 0;
    program->suspendingFrame = 0;
    program->resumingFrame = 0;
    program->occludedFrame = 0;
    program->startNs = NowNs();
}

// Whether a phase that checks the window can: its monitor's facts agree
// with it. An activity that starts turned can have its window before
// Android has turned the display's facts (Android 11), and the library
// reads them again every tenth of a second until the window fits; so
// where they did not agree when the window was ready, the phase waits
// for them, says how long it waited, and requires less than a second,
// well under the two seconds between the library's usual reads.
static bool MonitorReady(Program* program, mwinContext* context)
{
    if (program->phase != 0 && program->phase != 3 && program->phase != 6)
    {
        return true;
    }
    mwinMonitorId monitor = {0};
    mwinMonitorInfo info = {0};
    mwinWindowState state = StateOf(program, context);
    if (!MonitorOf(context, &monitor, &info) || !Agrees(&info, &state))
    {
        if (program->disagreedNs == 0)
        {
            program->disagreedNs = NowNs();
        }
        return false;
    }
    if (program->disagreedNs != 0)
    {
        uint64_t waitedNs = NowNs() - program->disagreedNs;
        printf("  the display's facts agreed with the window %.2f s after it was ready\n",
               (double)waitedNs / 1e9);
        CHECK(waitedNs < 1000000000u, "the display's facts follow the window within a second");
        program->disagreedNs = 0;
    }
    return true;
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
    if (program->phase == LAST_PHASE)
    {
        return mwin_frameStop;
    }
    if (Ready(program, context) && MonitorReady(program, context))
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
        if (program->disagreedNs != 0)
        {
            mwinMonitorId monitor = {0};
            mwinMonitorInfo info = {0};
            mwinWindowState state = StateOf(program, context);
            bool found = MonitorOf(context, &monitor, &info);
            printf("the display's facts never agreed with the window: %s %ux%u at scale %.3f, "
                   "the window %ux%u at scale %.3f\n",
                   found ? "the display" : "no display", info.bounds.width, info.bounds.height,
                   (double)info.scale, state.pixelSize.width, state.pixelSize.height,
                   (double)state.scale);
        }
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
    CHECK(program->phase == LAST_PHASE, "every phase ran");
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
