// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A program whose activity declares configChanges, in the emulator
// (tools/run_android_app.sh, which puts every setting back): Android
// keeps the activity and calls the library's onConfigurationChanged
// instead of making it anew. Turned the other way, the window's size and
// the screen's bounds follow; with the night mode and the font scale
// changed, the theme and the text scale follow. The theme comes only
// from the configuration that callback reads again, so it changes here
// only through it. Neither change loses the surface or suspends the
// program, which a new activity would.

#include "test_android_app.h"
#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/monitor.h"
#include "maul-window/native.h"
#include "maul-window/system.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define LAST_PHASE  2

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    // The activity the window showed in at the start.
    ANativeActivity* activity;
    bool wasPortrait;
    mwinTheme theme;
    // Told since the start.
    int resizes;
    int monitorChanges;
    int themeChanges;
    int surfacesLost;
    int suspends;
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
        if (program->phase == 0)
        {
            continue;
        }
        program->resizes += event.kind == mwin_eventResized;
        program->monitorChanges += event.kind == mwin_eventMonitorChanged;
        program->themeChanges += event.kind == mwin_eventThemeChanged;
        program->surfacesLost += event.kind == mwin_eventSurfaceLost;
        program->suspends += event.kind == mwin_eventSuspending;
    }
}

static mwinWindowState StateOf(const Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinWindow_GetState(context, program->window, &state);
    return state;
}

static mwinSystemFacts FactsOf(mwinContext* context)
{
    mwinSystemFacts facts = {0};
    (void)mwinContext_GetSystemFacts(context, &facts);
    return facts;
}

// The screen's bounds, or none before the library has read them.
static mwinPixelRect BoundsOf(mwinContext* context)
{
    mwinMonitorId monitor;
    size_t count = 0;
    mwinMonitorInfo info = {0};
    if (mwinContext_GetMonitors(context, &monitor, 1, &count) != mwin_success || count != 1 ||
        mwinMonitor_GetInfo(context, monitor, &info) != mwin_success)
    {
        return (mwinPixelRect){0};
    }
    return info.bounds;
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

static bool Turned(const Program* program, mwinContext* context)
{
    mwinWindowState state = StateOf(program, context);
    mwinPixelRect bounds = BoundsOf(context);
    bool portrait = state.pixelSize.height > state.pixelSize.width;
    return portrait != program->wasPortrait && (bounds.height > bounds.width) == portrait &&
           state.focused && HasSurface(context, program->window);
}

static bool Ready(Program* program, mwinContext* context)
{
    mwinSystemFacts facts = FactsOf(context);
    switch (program->phase)
    {
    case 0:
        return StateOf(program, context).focused && HasSurface(context, program->window) &&
               BoundsOf(context).width > 0;
    case 1:
        return Turned(program, context);
    case 2:
        return facts.theme != program->theme && fabsf(facts.textScale - 1.3f) < 0.01f &&
               HasSurface(context, program->window);
    default:
        return true;
    }
}

// The activity kept, its surface and the program's run unbroken.
static void CheckKept(const Program* program, mwinContext* context, const char* what)
{
    printf("after %s: %d resized, %d monitor changes, %d theme changes, %d surfaces lost, %d "
           "suspended\n",
           what, program->resizes, program->monitorChanges, program->themeChanges,
           program->surfacesLost, program->suspends);
    CHECK(ActivityWithSurface(context, program->window) == program->activity,
          "the same activity: Android told the change, it made none anew");
    CHECK(program->surfacesLost == 0 && program->suspends == 0,
          "no surface lost, no suspension: the activity kept");
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
    {
        Paint(program, context);
        mwinWindowState state = StateOf(program, context);
        mwinSystemFacts facts = FactsOf(context);
        program->activity = ActivityWithSurface(context, program->window);
        program->wasPortrait = state.pixelSize.height > state.pixelSize.width;
        program->theme = facts.theme;
        CHECK(facts.theme == mwin_themeLight || facts.theme == mwin_themeDark, "a theme");
        CHECK(fabsf(facts.textScale - 1.0f) < 0.01f, "the default text scale");
        printf("adb: settings put system accelerometer_rotation 0\n");
        printf("adb: settings put system user_rotation %d\n", program->wasPortrait ? 1 : 0);
        break;
    }
    case 1:
        CheckKept(program, context, "the turn");
        CHECK(program->resizes >= 1 && program->monitorChanges >= 1,
              "turned: the window resized and the screen changed");
        printf("adb: settings put system font_scale 1.3\n");
        printf("adb: cmd uimode night %s\n", program->theme == mwin_themeDark ? "no" : "yes");
        break;
    case 2:
        CheckKept(program, context, "the night mode and the font scale");
        CHECK(program->themeChanges >= 1, "the theme's change told");
        break;
    default:
        break;
    }
    program->phase += 1;
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
    if (program->phase >= 1)
    {
        // The turned surface shows a frame of its own.
        Paint(program, context);
    }
    if (Ready(program, context))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        mwinSystemFacts facts = FactsOf(context);
        mwinWindowState state = StateOf(program, context);
        mwinPixelRect bounds = BoundsOf(context);
        printf("timed out in phase %d: %ux%u pixels, screen %ux%u, theme %d, text scale %.2f\n",
               program->phase, state.pixelSize.width, state.pixelSize.height, bounds.width,
               bounds.height, (int)facts.theme, (double)facts.textScale);
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
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    return def;
}
