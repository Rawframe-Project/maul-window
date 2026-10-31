// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The services on Android, in the emulator (tools/run_android_app.sh):
// keeping awake sets the activity window's flag and clearing it clears
// it; showing a file, the message box, clipboard data and the primary
// selection are unsupported; an address opens in the browser, which
// takes the program to the background; an activity made anew while the
// window is kept awake keeps the display awake too. The clipboard's text
// is test_android_clipboard.c's.

#include "test_android_app.h"
#include "test_harness.h"

#include "maul-window/clipboard.h"
#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/services.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define LAST_PHASE  5

// WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON.
#define KEEP_SCREEN_ON 0x80

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    int completions;
    mwinOutcome outcomes[8];
    mwinRequestKind kinds[8];
    bool suspended;
    bool resumed;
    bool wasPortrait;
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
        if (event.type == mwin_eventRequestCompleted && program->completions < 8)
        {
            program->kinds[program->completions] = event.data.completion.kind;
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
        program->suspended |= event.type == mwin_eventSuspending;
        program->resumed |= event.type == mwin_eventResumed;
    }
}

static mwinWindowState StateOf(const Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinWindow_GetState(context, program->window, &state);
    return state;
}

// Calls a method of an object by name; an object result as a local
// reference.
static jobject CallObject(JNIEnv* env, jobject object, const char* name, const char* signature)
{
    jclass type = (*env)->GetObjectClass(env, object);
    jobject result =
        (*env)->CallObjectMethod(env, object, (*env)->GetMethodID(env, type, name, signature));
    (*env)->DeleteLocalRef(env, type);
    return result;
}

// The activity's window's flags (WindowManager.LayoutParams.flags) into
// flagsOut; false, and no flags, while the window has no surface.
static bool WindowFlags(const Program* program, mwinContext* context, jint* flagsOut)
{
    *flagsOut = 0;
    ANativeActivity* activity = ActivityWithSurface(context, program->window);
    if (activity == nullptr)
    {
        return false;
    }
    JNIEnv* env = activity->env;
    jobject window = CallObject(env, activity->clazz, "getWindow", "()Landroid/view/Window;");
    jobject attributes =
        CallObject(env, window, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
    jclass type = (*env)->GetObjectClass(env, attributes);
    *flagsOut = (*env)->GetIntField(env, attributes, (*env)->GetFieldID(env, type, "flags", "I"));
    (*env)->DeleteLocalRef(env, type);
    (*env)->DeleteLocalRef(env, attributes);
    (*env)->DeleteLocalRef(env, window);
    return true;
}

// Whether the activity's window keeps the screen on, or off when on is
// false; never while the window has no surface to read it from.
static bool Kept(const Program* program, mwinContext* context, bool on)
{
    jint flags = 0;
    return WindowFlags(program, context, &flags) && ((flags & KEEP_SCREEN_ON) != 0) == on;
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

static bool Ready(Program* program, mwinContext* context)
{
    mwinWindowState state = StateOf(program, context);
    switch (program->phase)
    {
    case 0:
        return state.focused;
    // NativeActivity sets the window's flags later, on its Java side:
    // the test waits for them.
    case 1:
        return program->completions >= 1 && Kept(program, context, true);
    case 2:
        return program->completions >= 4 && Kept(program, context, false);
    case 3:
        return program->completions >= 2 && program->suspended;
    case 4:
        return program->resumed && state.focused;
    case 5:
        return (state.pixelSize.width > state.pixelSize.height) == program->wasPortrait &&
               state.focused && Kept(program, context, true);
    default:
        return true;
    }
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
        CHECK(mwinWindow_RequestKeepAwake(context, program->window, true, nullptr) == mwin_success,
              "keeping awake asked for");
        break;
    case 1:
        CHECK(outcomes[0] == mwin_outcomeDone, "keeping awake done");
        CHECK(Kept(program, context, true) && StateOf(program, context).awake,
              "kept awake: the window's flag");
        CHECK(mwinWindow_RequestKeepAwake(context, program->window, false, nullptr) ==
                      mwin_success &&
                  mwinWindow_RequestRevealFile(context, program->window, "/sdcard", 7, nullptr) ==
                      mwin_success,
              "no longer awake, and a file shown, asked for");
        CHECK(mwinWindow_RequestClipboardReadData(context, program->window, "image/png", 9,
                                                  nullptr) == mwin_success &&
                  mwinWindow_RequestPrimaryRead(context, program->window, nullptr) == mwin_success,
              "data and the primary selection asked for");
        {
            mwinMessageBoxDef box = mwinDefaultMessageBoxDef();
            CHECK(mwinShowMessageBox(&box, nullptr) == mwin_errorUnsupported,
                  "no message box to wait for");
        }
        break;
    case 2:
        CHECK(outcomes[0] == mwin_outcomeDone && outcomes[1] == mwin_outcomeUnsupported,
              "awake cleared; no file manager");
        CHECK(outcomes[2] == mwin_outcomeUnsupported && outcomes[3] == mwin_outcomeUnsupported,
              "no clipboard data, no primary selection");
        CHECK(Kept(program, context, false) && !StateOf(program, context).awake,
              "the window's flag cleared");
        {
            const char url[] = "https://example.com/maul";
            CHECK(mwinWindow_RequestKeepAwake(context, program->window, true, nullptr) ==
                          mwin_success &&
                      mwinWindow_RequestOpenUrl(context, program->window, url, sizeof(url) - 1,
                                                nullptr) == mwin_success,
                  "awake again, and an address opened");
        }
        break;
    case 3:
        CHECK(outcomes[0] == mwin_outcomeDone && outcomes[1] == mwin_outcomeDone,
              "awake again, and the address opened in the browser");
        printf("adb: am start -n " MWIN_TEST_PACKAGE "/maul.window.Activity\n");
        break;
    case 4:
    {
        // Turned the other way: the activity is made anew and joins.
        mwinWindowState state = StateOf(program, context);
        program->wasPortrait = state.pixelSize.height > state.pixelSize.width;
        printf("adb: settings put system accelerometer_rotation 0\n");
        printf("adb: settings put system user_rotation %d\n", program->wasPortrait ? 1 : 0);
        break;
    }
    case 5:
        CHECK(Kept(program, context, true), "the activity made anew kept awake too");
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
