// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The on-screen keyboard asked for another purpose on Android, in the
// emulator (tools/run_android_app.sh): the keyboard, shown, is hidden,
// then asked for text, then a number, then an address, so that input
// starts anew for each, in rounds: each request once the last completed,
// or all three in one frame. Each round, once the requests completed, the
// view gives input methods an address's input type. Whether the keyboard
// then shows is the input method's: Gboard on Android 11, running behind,
// can hide it by itself, so the test does not wait for it.

#include "test_android_app.h"
#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define ROUNDS      6
// The phases of a round, after the keyboard first shown.
#define ROUND      4
#define LAST_PHASE (1 + ROUNDS * ROUND)

// Android's input types (android.text.InputType).
#define TYPE_CLASS_TEXT               0x1
#define TYPE_TEXT_VARIATION_EMAIL     0x20
#define TYPE_TEXT_FLAG_NO_SUGGESTIONS 0x80000

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    int completions;
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
        program->shown |= event.kind == mwin_eventShown;
        program->completions += event.kind == mwin_eventRequestCompleted;
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

// The input type the activity's focused view gives an input method.
static jint TypeOf(JNIEnv* env, ANativeActivity* activity)
{
    jobject window = CallObject(env, activity->clazz, "getWindow", "()Landroid/view/Window;");
    jobject decor = CallObject(env, window, "getDecorView", "()Landroid/view/View;");
    jobject field = CallObject(env, decor, "findFocus", "()Landroid/view/View;");
    jclass infos = (*env)->FindClass(env, "android/view/inputmethod/EditorInfo");
    jobject info = (*env)->NewObject(env, infos, (*env)->GetMethodID(env, infos, "<init>", "()V"));
    if (field != nullptr)
    {
        jclass views = (*env)->FindClass(env, "android/view/View");
        jmethodID create = (*env)->GetMethodID(
            env, views, "onCreateInputConnection",
            "(Landroid/view/inputmethod/EditorInfo;)Landroid/view/inputmethod/InputConnection;");
        (*env)->DeleteLocalRef(env, (*env)->CallObjectMethod(env, field, create, info));
        (*env)->DeleteLocalRef(env, views);
    }
    jint type = (*env)->GetIntField(env, info, (*env)->GetFieldID(env, infos, "inputType", "I"));
    jobject locals[] = {window, decor, field, infos, info};
    for (size_t i = 0; i < sizeof(locals) / sizeof(locals[0]); i++)
    {
        (*env)->DeleteLocalRef(env, locals[i]);
    }
    return type;
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

// A round's step: 0 waits for the keyboard hidden and asks for it for
// text, 1 and 2 for a number and an address, and 3 checks the address's
// input type and hides it for the next round.
static int StepOf(const Program* program)
{
    return (program->phase - 2) % ROUND;
}

// Odd rounds ask for every purpose in one frame.
static bool AtOnce(const Program* program)
{
    return (program->phase - 2) / ROUND % 2 == 1;
}

static bool Ready(const Program* program, mwinContext* context)
{
    float height = StateOf(program, context).virtualKeyboard.height;
    if (program->phase == 0)
    {
        return program->shown && HasSurface(context, program->window);
    }
    if (program->phase == 1)
    {
        return program->completions >= 1 && height > 0.0f;
    }
    switch (StepOf(program))
    {
    case 0:
        return program->completions >= 1 && height == 0.0f;
    case 1:
    case 2:
        return AtOnce(program) || program->completions >= 1;
    default:
        return program->completions >= (AtOnce(program) ? 3 : 1);
    }
}

// Asks for the keyboard for a purpose, once the last request completed.
static void Ask(Program* program, mwinContext* context, mwinInputPurpose purpose, const char* what)
{
    program->completions = 0;
    CHECK(mwinWindow_RequestVirtualKeyboard(context, program->window, true, purpose, nullptr) ==
              mwin_success,
          what);
}

// Checks the view gives input methods an address's input type.
static void CheckAddress(mwinContext* context, const Program* program)
{
    ANativeActivity* activity = ActivityWithSurface(context, program->window);
    CHECK(activity != nullptr, "the activity, to ask its connection");
    CHECK(activity != nullptr &&
              TypeOf(activity->env, activity) ==
                  (TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_EMAIL | TYPE_TEXT_FLAG_NO_SUGGESTIONS),
          "an address's input type");
}

static void Advance(Program* program, mwinContext* context)
{
    if (program->phase == 0)
    {
        Paint(program, context);
        CHECK(mwinWindow_RequestTextInput(context, program->window, true, nullptr) == mwin_success,
              "text input asked for");
        Ask(program, context, mwin_purposeText, "a keyboard for text");
    }
    else if (program->phase == 1 || StepOf(program) == 3)
    {
        if (program->phase > 1)
        {
            CheckAddress(context, program);
        }
        if (program->phase < LAST_PHASE)
        {
            program->completions = 0;
            CHECK(mwinWindow_RequestVirtualKeyboard(context, program->window, false,
                                                    mwin_purposeEmail, nullptr) == mwin_success,
                  "the keyboard hidden");
        }
    }
    else if (StepOf(program) == 0)
    {
        Ask(program, context, mwin_purposeText, "a keyboard for text");
        if (AtOnce(program))
        {
            CHECK(mwinWindow_RequestVirtualKeyboard(context, program->window, true,
                                                    mwin_purposeNumber, nullptr) == mwin_success &&
                      mwinWindow_RequestVirtualKeyboard(context, program->window, true,
                                                        mwin_purposeEmail, nullptr) == mwin_success,
                  "a keyboard for a number and an address, in the same frame");
        }
    }
    else if (!AtOnce(program))
    {
        Ask(program, context, StepOf(program) == 1 ? mwin_purposeNumber : mwin_purposeEmail,
            StepOf(program) == 1 ? "a keyboard for a number" : "a keyboard for an address");
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
    if (Ready(program, context))
    {
        printf("phase %d\n", program->phase);
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf(
            "timed out in phase %d (round %d, step %d), %d completions, the keyboard %.0f high\n",
            program->phase, (program->phase - 2) / ROUND, StepOf(program), program->completions,
            (double)StateOf(program, context).virtualKeyboard.height);
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
