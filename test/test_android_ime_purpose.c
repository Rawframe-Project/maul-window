// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The on-screen keyboard asked for another purpose on Android, in the
// emulator (tools/run_android_app.sh): from hidden, the keyboard is asked
// for text, then a number, then an address, each once the last request
// completed, so that input starts anew for each; it shows, for an
// address. Android 11's input method can drop a keyboard shown as input
// starts anew, so the test is quarantined (CMakeLists.txt).

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
#define LAST_PHASE  3

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

static bool Ready(const Program* program, mwinContext* context)
{
    mwinWindowState state = StateOf(program, context);
    switch (program->phase)
    {
    case 0:
        return program->shown && HasSurface(context, program->window);
    case 1:
    case 2:
        return program->completions >= 1;
    case 3:
        return program->completions >= 1 && state.virtualKeyboard.height > 0.0f;
    default:
        return true;
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

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
        Paint(program, context);
        CHECK(mwinWindow_RequestTextInput(context, program->window, true, nullptr) == mwin_success,
              "text input asked for");
        Ask(program, context, mwin_purposeText, "a keyboard for text");
        break;
    case 1:
        Ask(program, context, mwin_purposeNumber, "a keyboard for a number");
        break;
    case 2:
        Ask(program, context, mwin_purposeEmail, "a keyboard for an address");
        break;
    case 3:
    {
        ANativeActivity* activity = ActivityWithSurface(context, program->window);
        CHECK(activity != nullptr, "the activity, to ask its connection");
        CHECK(activity != nullptr &&
                  TypeOf(activity->env, activity) ==
                      (TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_EMAIL | TYPE_TEXT_FLAG_NO_SUGGESTIONS),
              "the keyboard shown, for an address");
        break;
    }
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
    if (Ready(program, context))
    {
        printf("phase %d\n", program->phase);
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d, %d completions, the keyboard %.0f high\n", program->phase,
               program->completions, (double)StateOf(program, context).virtualKeyboard.height);
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
