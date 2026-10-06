// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The mouse pointer's icon on Android, in the emulator
// (tools/run_android_app.sh), read back from the activity's view: a
// shape is the system's icon of its type; a cursor made from images is
// an icon of its own, which a shape replaces and which comes back; an
// activity made anew (a rotation) shows it too; destroyed, it leaves
// the arrow.

#include "cursor_images.h"
#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <stdlib.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define LAST_PHASE  5

// PointerIcon's types.
#define TYPE_ARROW 1000
#define TYPE_TEXT  1008

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinCursorId cursor;
    int completions;
    mwinOutcome outcome;
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
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventRequestCompleted)
        {
            program->completions += 1;
            program->outcome = event.data.completion.outcome;
        }
    }
}

static mwinWindowState StateOf(const Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinGetWindowState(context, program->window, &state);
    return state;
}

// What the view shows: the system's icon of a type (1), another icon
// (2), or none (0).
static int Shows(const Program* program, mwinContext* context, jint type)
{
    mwinNativeHandles handles = {0};
    if (mwinGetNativeHandles(context, program->window, &handles) != mwin_success)
    {
        return 0;
    }
    ANativeActivity* activity = handles.handles.android.activity;
    jobject view = handles.handles.android.view;
    JNIEnv* env = activity->env;
    jclass views = (*env)->GetObjectClass(env, view);
    jobject shown = (*env)->CallObjectMethod(
        env, view,
        (*env)->GetMethodID(env, views, "getPointerIcon", "()Landroid/view/PointerIcon;"));
    jclass icons = (*env)->FindClass(env, "android/view/PointerIcon");
    jobject system = (*env)->CallStaticObjectMethod(
        env, icons,
        (*env)->GetStaticMethodID(env, icons, "getSystemIcon",
                                  "(Landroid/content/Context;I)Landroid/view/PointerIcon;"),
        activity->clazz, type);
    jboolean same =
        shown != nullptr &&
        (*env)->CallBooleanMethod(
            env, system, (*env)->GetMethodID(env, icons, "equals", "(Ljava/lang/Object;)Z"), shown);
    int result = shown == nullptr ? 0 : same ? 1 : 2;
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, system);
    (*env)->DeleteLocalRef(env, icons);
    (*env)->DeleteLocalRef(env, shown);
    (*env)->DeleteLocalRef(env, views);
    return result;
}

// The cursor made from images: neither the arrow nor the text icon.
static bool ShowsImages(const Program* program, mwinContext* context)
{
    return Shows(program, context, TYPE_ARROW) == 2 && Shows(program, context, TYPE_TEXT) == 2;
}

static bool Ready(Program* program, mwinContext* context)
{
    mwinWindowState state = StateOf(program, context);
    switch (program->phase)
    {
    case 0:
        return state.focused;
    case 5:
        return (state.pixelSize.width > state.pixelSize.height) == program->wasPortrait &&
               state.focused;
    default:
        return program->completions >= 1;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    bool done = program->completions >= 1 && program->outcome == mwin_outcomeDone;
    switch (program->phase)
    {
    case 0:
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a text cursor");
        break;
    case 1:
    {
        CHECK(done && Shows(program, context, TYPE_TEXT) == 1, "the system's text icon");
        mwinIconImage images[2];
        mwinCursorDef def = CursorImagesDef(images);
        CHECK(mwinCreateCursor(context, &def, &program->cursor) == mwin_success &&
                  mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                      mwin_success,
              "a cursor made from images");
        break;
    }
    case 2:
        CHECK(done && ShowsImages(program, context), "an icon of the images");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a shape again");
        break;
    case 3:
        CHECK(done && Shows(program, context, TYPE_TEXT) == 1, "the shape in the images' place");
        CHECK(mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                  mwin_success,
              "the images again");
        break;
    case 4:
    {
        CHECK(done && ShowsImages(program, context), "the images in the shape's place");
        // Turned the other way: the activity is made anew and joins.
        mwinWindowState state = StateOf(program, context);
        program->wasPortrait = state.pixelSize.height > state.pixelSize.width;
        printf("adb: settings put system accelerometer_rotation 0\n");
        printf("adb: settings put system user_rotation %d\n", program->wasPortrait ? 1 : 0);
        break;
    }
    case 5:
        CHECK(ShowsImages(program, context), "the activity made anew shows the images");
        CHECK(mwinDestroyCursor(context, program->cursor) == mwin_success &&
                  Shows(program, context, TYPE_ARROW) == 1,
              "destroyed: the arrow");
        break;
    default:
        break;
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
