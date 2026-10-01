// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's facts, locales and Back on Android, in the emulator
// (tools/run_android_app.sh, which puts every setting back): reduced
// motion while the runner's animator scale is 0; the accent from
// Android 12; preferred locales; the battery unplugged, told within the
// power's look, then battery saver on; the night mode and the font scale
// changed, told through the activity made anew; Back, held, asking the
// window to close once, on its release, the program going on; and a Back
// whose release Android cancels (a back gesture pulled away), injected
// into the test's own window from a thread of the test's, asking
// nothing (where Android lets an application inject into its own window:
// Android 11 does, Android 15 asks for INJECT_EVENTS).

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/system.h"
#include "maul-window/window.h"

#include <android/api-level.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define LAST_PHASE  6

// KeyEvent's codes and flags.
#define KEYCODE_BACK  4
#define ACTION_DOWN   0
#define ACTION_UP     1
#define FLAG_CANCELED 0x20

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool themeChanged;
    bool powerChanged;
    bool localeChanged;
    int closeRequests;
    mwinTheme theme;
    int framesAfterClose;
    ANativeActivity* activity;
    pthread_t injector;
    atomic_bool injected;
    atomic_bool refused;
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
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->themeChanged |= event.type == mwin_eventThemeChanged;
        program->powerChanged |= event.type == mwin_eventPowerChanged;
        program->localeChanged |= event.type == mwin_eventLocaleChanged;
        program->closeRequests += event.type == mwin_eventCloseRequested;
    }
}

static mwinSystemFacts FactsOf(mwinContext* context)
{
    mwinSystemFacts facts = {0};
    (void)mwinGetSystemFacts(context, &facts);
    return facts;
}

// Shows a frame, drawn on the CPU: Android 14 and later send nothing to
// a window whose surface never showed one.
static void Paint(const Program* program, mwinContext* context)
{
    mwinNativeHandles handles = {0};
    if (mwinGetNativeHandles(context, program->window, &handles) != mwin_success)
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

// Injects Back, pressed and its release cancelled, into the test's own
// window: Instrumentation waits for each to be handled, which the main
// thread does, so it runs here.
static void* InjectCancelledBack(void* user)
{
    Program* program = user;
    JavaVM* vm = program->activity->vm;
    JNIEnv* env = nullptr;
    if ((*vm)->AttachCurrentThread(vm, &env, nullptr) != JNI_OK)
    {
        atomic_store(&program->injected, true);
        return nullptr;
    }
    jclass instruments = (*env)->FindClass(env, "android/app/Instrumentation");
    jobject instrument =
        (*env)->NewObject(env, instruments, (*env)->GetMethodID(env, instruments, "<init>", "()V"));
    jmethodID send =
        (*env)->GetMethodID(env, instruments, "sendKeySync", "(Landroid/view/KeyEvent;)V");
    jclass keys = (*env)->FindClass(env, "android/view/KeyEvent");
    jmethodID make = (*env)->GetMethodID(env, keys, "<init>", "(JJIIIIIII)V");
    jlong now = (jlong)(NowNs() / 1000000u);
    jobject down =
        (*env)->NewObject(env, keys, make, now, now, ACTION_DOWN, KEYCODE_BACK, 0, 0, -1, 0, 0);
    jobject up = (*env)->NewObject(env, keys, make, now, now + 50, ACTION_UP, KEYCODE_BACK, 0, 0,
                                   -1, 0, FLAG_CANCELED);
    (*env)->CallVoidMethod(env, instrument, send, down);
    bool refused = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    if (!refused)
    {
        (*env)->CallVoidMethod(env, instrument, send, up);
        refused = (*env)->ExceptionCheck(env);
        (*env)->ExceptionClear(env);
    }
    atomic_store(&program->refused, refused);
    (*vm)->DetachCurrentThread(vm);
    atomic_store(&program->injected, true);
    return nullptr;
}

static bool Ready(Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinGetWindowState(context, program->window, &state);
    mwinSystemFacts facts = FactsOf(context);
    switch (program->phase)
    {
    case 0:
        return state.focused;
    case 1:
        // Only the power's look can see this: unplugging changes nothing
        // else.
        return program->powerChanged && facts.onBattery == mwin_yes;
    case 2:
        return facts.lowPower == mwin_yes;
    case 3:
        return program->themeChanged && facts.theme != program->theme &&
               fabsf(facts.textScale - 1.3f) < 0.01f && state.focused;
    case 4:
        return program->closeRequests > 0;
    case 5:
        // The program goes on after Back.
        program->framesAfterClose += 1;
        return program->framesAfterClose > 30;
    case 6:
        // Frames after the cancelled Back, for anything it asked.
        program->framesAfterClose += atomic_load(&program->injected) ? 1 : 0;
        return program->framesAfterClose > 60;
    default:
        return true;
    }
}

static void CheckStart(Program* program, mwinContext* context)
{
    mwinSystemFacts facts = FactsOf(context);
    CHECK(facts.reducedMotion, "reduced motion while animations are off");
    CHECK(facts.hasAccent == (android_get_device_api_level() >= 31), "an accent from Android 12");
    CHECK(!facts.hasAccent || (facts.accent & 0xFFu) == 0xFFu, "the accent opaque");
    CHECK(facts.theme == mwin_themeLight || facts.theme == mwin_themeDark, "a theme");
    CHECK(fabsf(facts.textScale - 1.0f) < 0.01f, "the default text scale");
    CHECK(facts.onBattery == mwin_no && facts.lowPower == mwin_no, "plugged in, no battery saver");
    char locales[256] = {0};
    size_t length = 0;
    CHECK(mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success &&
              length >= 2 && locales[0] >= 'a' && locales[0] <= 'z',
          "preferred locales");
    program->theme = facts.theme;
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
        Paint(program, context);
        CheckStart(program, context);
        printf("adb: dumpsys battery unplug\n");
        break;
    case 1:
        printf("adb: settings put global low_power 1\n");
        break;
    case 2:
        printf("adb: settings put global low_power 0\n");
        printf("adb: dumpsys battery reset\n");
        printf("adb: settings put system font_scale 1.3\n");
        printf("adb: cmd uimode night %s\n", program->theme == mwin_themeDark ? "no" : "yes");
        break;
    case 3:
        // Held: its repeats ask nothing.
        printf("adb: input keyevent --longpress KEYCODE_BACK\n");
        break;
    case 5:
        CHECK(program->closeRequests == 1, "Back, held, asked once");
        {
            // The activity now: the night mode made it anew.
            mwinNativeHandles handles = {0};
            (void)mwinGetNativeHandles(context, program->window, &handles);
            program->activity = handles.handles.android.activity;
        }
        CHECK(program->activity != nullptr &&
                  pthread_create(&program->injector, nullptr, InjectCancelledBack, program) == 0,
              "the injector started");
        program->framesAfterClose = 0;
        break;
    case 6:
        (void)pthread_join(program->injector, nullptr);
        if (atomic_load(&program->refused))
        {
            printf("a cancelled Back not injected: Android refuses injection here\n");
        }
        CHECK(program->closeRequests == 1, "a cancelled Back asks nothing");
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
    if (program->phase >= 2 && program->phase <= 4)
    {
        // The activity made anew shows a frame of its own.
        Paint(program, context);
    }
    if (Ready(program, context))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d\n", program->phase);
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
