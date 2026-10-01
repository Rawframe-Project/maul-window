// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility on Android, in the emulator (tools/run_android_app.sh):
// the test's tree (test/android/java/maul/window/tests/Tree.java) as the
// window's root, read by an accessibility client of the test's own
// (Client.java), which the runner enables and which asks for touch
// exploration: the client finds the tree's nodes through the system,
// the window tells the program a client asked; with no root the client
// still reads the window but nothing of the tree; and with the root
// again, a finger the emulator's console puts on the right half is
// explored, announcing "Beta" and reaching the program as no touch.

#include "test_harness.h"

#include "maul-window/accessibility.h"
#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define LAST_PHASE  6

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    ANativeActivity* activity;
    jobject tree;
    int completions;
    mwinOutcome outcomes[4];
    bool asked;
    int touches;
    mwinPixelSize size;
    pthread_t reader;
    atomic_bool described;
    int changes;
    char texts[256];
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
        if (event.type == mwin_eventRequestCompleted && program->completions < 4)
        {
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
        program->asked |= event.type == mwin_eventAccessibilityRequested;
        program->touches +=
            event.type == mwin_eventTouchDown || event.type == mwin_eventTouchMoved ||
            event.type == mwin_eventCursorMoved || event.type == mwin_eventCursorEntered ||
            event.type == mwin_eventPenMoved;
    }
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

// A class of the test's application, through its class loader.
static jclass LoadClass(JNIEnv* env, ANativeActivity* activity, const char* name)
{
    jclass activities = (*env)->GetObjectClass(env, activity->clazz);
    jobject loader = (*env)->CallObjectMethod(
        env, activity->clazz,
        (*env)->GetMethodID(env, activities, "getClassLoader", "()Ljava/lang/ClassLoader;"));
    jclass loaders = (*env)->GetObjectClass(env, loader);
    jstring text = (*env)->NewStringUTF(env, name);
    jclass type = (*env)->CallObjectMethod(
        env, loader,
        (*env)->GetMethodID(env, loaders, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"),
        text);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, text);
    (*env)->DeleteLocalRef(env, loaders);
    (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, activities);
    return type;
}

static jboolean Exploring(const Program* program)
{
    JNIEnv* env = program->activity->env;
    jclass clients = LoadClass(env, program->activity, "maul.window.tests.Client");
    jboolean exploring =
        clients != nullptr &&
        (*env)->CallStaticBooleanMethod(
            env, clients, (*env)->GetStaticMethodID(env, clients, "exploring", "()Z"));
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, clients);
    return exploring;
}

// The text of the last node a hover entered, as the client heard it.
static bool Entered(const Program* program, const char* name)
{
    JNIEnv* env = program->activity->env;
    jclass clients = LoadClass(env, program->activity, "maul.window.tests.Client");
    jstring text =
        clients != nullptr
            ? (*env)->GetStaticObjectField(
                  env, clients,
                  (*env)->GetStaticFieldID(env, clients, "entered", "Ljava/lang/String;"))
            : nullptr;
    const char* bytes = text != nullptr ? (*env)->GetStringUTFChars(env, text, nullptr) : nullptr;
    bool same = bytes != nullptr && strcmp(bytes, name) == 0;
    if (bytes != nullptr)
    {
        (*env)->ReleaseStringUTFChars(env, text, bytes);
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, text);
    (*env)->DeleteLocalRef(env, clients);
    return same;
}

static void ForgetRead(const Program* program)
{
    JNIEnv* env = program->activity->env;
    jclass trees = LoadClass(env, program->activity, "maul.window.tests.Tree");
    if (trees != nullptr)
    {
        (*env)->SetStaticIntField(env, trees, (*env)->GetStaticFieldID(env, trees, "read", "I"), 0);
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, trees);
}

static int Changes(const Program* program)
{
    JNIEnv* env = program->activity->env;
    jclass clients = LoadClass(env, program->activity, "maul.window.tests.Client");
    jint changes = clients != nullptr
                       ? (*env)->GetStaticIntField(
                             env, clients, (*env)->GetStaticFieldID(env, clients, "changes", "I"))
                       : 0;
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, clients);
    return changes;
}

static int TreeRead(const Program* program)
{
    JNIEnv* env = program->activity->env;
    jclass trees = LoadClass(env, program->activity, "maul.window.tests.Tree");
    jint read = trees != nullptr
                    ? (*env)->GetStaticIntField(env, trees,
                                                (*env)->GetStaticFieldID(env, trees, "read", "I"))
                    : 0;
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, trees);
    return read;
}

// Reads the active window's texts through the client, on a thread of its
// own: the window answers on the main thread.
static void* Describe(void* user)
{
    Program* program = user;
    JavaVM* vm = program->activity->vm;
    JNIEnv* env = nullptr;
    if ((*vm)->AttachCurrentThread(vm, &env, nullptr) == JNI_OK)
    {
        jclass clients = LoadClass(env, program->activity, "maul.window.tests.Client");
        jstring texts =
            clients != nullptr
                ? (*env)->CallStaticObjectMethod(
                      env, clients,
                      (*env)->GetStaticMethodID(env, clients, "describe", "()Ljava/lang/String;"))
                : nullptr;
        (*env)->ExceptionClear(env);
        const char* bytes =
            texts != nullptr ? (*env)->GetStringUTFChars(env, texts, nullptr) : nullptr;
        program->texts[0] = '\0';
        if (bytes != nullptr)
        {
            strncat(program->texts, bytes, sizeof(program->texts) - 1);
            (*env)->ReleaseStringUTFChars(env, texts, bytes);
        }
        (*vm)->DetachCurrentThread(vm);
    }
    atomic_store(&program->described, true);
    return nullptr;
}

static void StartDescribing(Program* program)
{
    atomic_store(&program->described, false);
    CHECK(pthread_create(&program->reader, nullptr, Describe, program) == 0, "a reader started");
}

static bool Described(Program* program)
{
    if (!atomic_load(&program->described))
    {
        return false;
    }
    (void)pthread_join(program->reader, nullptr);
    return true;
}

static bool Ready(Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinGetWindowState(context, program->window, &state);
    switch (program->phase)
    {
    case 0:
        return state.focused;
    case 1:
        return program->completions >= 1 && Exploring(program);
    case 2:
    case 4:
        return Described(program);
    case 3:
        // Clients told to read the tree again.
        return program->completions >= 2 && Changes(program) > program->changes;
    case 5:
        return program->completions >= 3;
    case 6:
        return Entered(program, "Beta");
    default:
        return true;
    }
}

static void SetRoot(Program* program, mwinContext* context, jobject root)
{
    CHECK(mwinRequestAccessibilityRoot(context, program->window, root, nullptr) == mwin_success,
          "the root asked for");
}

static void Start(Program* program, mwinContext* context)
{
    Paint(program, context);
    mwinNativeHandles handles = {0};
    (void)mwinGetNativeHandles(context, program->window, &handles);
    program->activity = handles.handles.android.activity;
    mwinWindowState state = {0};
    (void)mwinGetWindowState(context, program->window, &state);
    program->size = state.pixelSize;
    jobject view = handles.handles.android.view;
    CHECK(view != nullptr, "the view in the handles");
    JNIEnv* env = program->activity->env;
    jclass trees = LoadClass(env, program->activity, "maul.window.tests.Tree");
    jobject tree =
        trees != nullptr
            ? (*env)->NewObject(env, trees,
                                (*env)->GetMethodID(env, trees, "<init>", "(Landroid/view/View;)V"),
                                view)
            : nullptr;
    program->tree = tree != nullptr ? (*env)->NewGlobalRef(env, tree) : nullptr;
    (*env)->DeleteLocalRef(env, tree);
    (*env)->DeleteLocalRef(env, trees);
    CHECK(program->tree != nullptr, "a tree made");
    SetRoot(program, context, program->tree);
    printf("adb: settings put secure enabled_accessibility_services " MWIN_TEST_PACKAGE
           "/maul.window.tests.Client\n");
    printf("adb: settings put secure accessibility_enabled 1\n");
}

static void Explore(Program* program)
{
    CHECK(program->outcomes[2] == mwin_outcomeDone, "the root set again");
    program->touches = 0;
    int x = (int)program->size.width * 3 / 4;
    int y = (int)program->size.height / 2;
    printf("emu: event mouse %d %d 0 1\n", x, y);
    printf("emu: event mouse %d %d 0 1\n", x + 10, y + 10);
    printf("emu: event mouse %d %d 0 0\n", x + 10, y + 10);
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
        Start(program, context);
        break;
    case 1:
        CHECK(program->outcomes[0] == mwin_outcomeDone, "the root set");
        StartDescribing(program);
        break;
    case 2:
        CHECK(strstr(program->texts, "Alpha,") != nullptr &&
                  strstr(program->texts, "Beta,") != nullptr,
              "the client found the tree's nodes");
        CHECK(TreeRead(program) == 7, "every node read from the tree");
        CHECK(program->asked, "the program told a client asked");
        program->changes = Changes(program);
        SetRoot(program, context, nullptr);
        break;
    case 3:
        CHECK(program->outcomes[1] == mwin_outcomeDone, "no root set");
        ForgetRead(program);
        StartDescribing(program);
        break;
    case 4:
        CHECK(atoi(program->texts) >= 1, "with no root, the window still read");
        CHECK(strstr(program->texts, "Alpha,") == nullptr && TreeRead(program) == 0,
              "with no root, nothing of the tree");
        SetRoot(program, context, program->tree);
        break;
    case 5:
        Explore(program);
        break;
    case 6:
        CHECK(program->touches == 0, "the explored finger no touch of the program's");
        (*program->activity->env)->DeleteGlobalRef(program->activity->env, program->tree);
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
    if (Ready(program, context))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d (texts \"%s\")\n", program->phase, program->texts);
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
