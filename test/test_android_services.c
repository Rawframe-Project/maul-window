// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard and services on Android, in the emulator
// (tools/run_android_app.sh): text written to the clipboard reads back
// the same, as do ASCII at the limit and empty text; keeping awake sets
// the activity window's flag and clearing it clears it; showing a file,
// the message box, clipboard data and the primary selection (mwin-0029)
// are unsupported; an address opens in the browser, which takes the
// program to the background; an activity made anew while the window is
// kept awake keeps the display awake too.

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
#define LAST_PHASE  6

// WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON.
#define KEEP_SCREEN_ON 0x80

// Text past ASCII, in one, two and three bytes.
#define TEXT "H\xc3\xa9llo, \xe2\x9c\x93 w\xc3\xb6rld"
// ASCII as long as TEXT in bytes, the clipboard's limit: as many units.
#define LIMIT_TEXT "maul-window-limits"
static_assert(sizeof(LIMIT_TEXT) == sizeof(TEXT), "the limit is TEXT's bytes");

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    int completions;
    mwinOutcome outcomes[8];
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
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventRequestCompleted && program->completions < 8)
        {
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
        program->suspended |= event.type == mwin_eventSuspending;
        program->resumed |= event.type == mwin_eventResumed;
    }
}

static mwinWindowState StateOf(const Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinGetWindowState(context, program->window, &state);
    return state;
}

static ANativeActivity* ActivityOf(const Program* program, mwinContext* context)
{
    mwinNativeHandles handles = {0};
    (void)mwinGetNativeHandles(context, program->window, &handles);
    return handles.handles.android.activity;
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

// The activity's window's flags (WindowManager.LayoutParams.flags).
static jint WindowFlags(const Program* program, mwinContext* context)
{
    ANativeActivity* activity = ActivityOf(program, context);
    JNIEnv* env = activity->env;
    jobject window = CallObject(env, activity->clazz, "getWindow", "()Landroid/view/Window;");
    jobject attributes =
        CallObject(env, window, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
    jclass type = (*env)->GetObjectClass(env, attributes);
    jint flags = (*env)->GetIntField(env, attributes, (*env)->GetFieldID(env, type, "flags", "I"));
    (*env)->DeleteLocalRef(env, type);
    (*env)->DeleteLocalRef(env, attributes);
    (*env)->DeleteLocalRef(env, window);
    return flags;
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
        return program->completions >= 2 && (WindowFlags(program, context) & KEEP_SCREEN_ON) != 0;
    case 3:
        return program->completions >= 6 && (WindowFlags(program, context) & KEEP_SCREEN_ON) == 0;
    case 2:
        return program->completions >= 1;
    case 4:
        return program->completions >= 4 && program->suspended;
    case 5:
        return program->resumed && state.focused;
    case 6:
        return (state.pixelSize.width > state.pixelSize.height) == program->wasPortrait &&
               state.focused && (WindowFlags(program, context) & KEEP_SCREEN_ON) != 0;
    default:
        return true;
    }
}

// The last clipboard read asked, whose payload the checks copy out.
static mwinRequestId s_read;

static bool ReadBack(mwinContext* context, const char* expected)
{
    char text[64] = {0};
    size_t length = 0;
    return mwinGetClipboardText(context, s_read, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

// Writes text and reads it back: two requests.
static void WriteRead(const Program* program, mwinContext* context, const char* text)
{
    CHECK(mwinRequestClipboardWrite(context, program->window, text, strlen(text), nullptr) ==
                  mwin_success &&
              mwinRequestClipboardRead(context, program->window, &s_read) == mwin_success,
          "a write and a read asked for");
}

static void Advance(Program* program, mwinContext* context)
{
    const mwinOutcome* outcomes = program->outcomes;
    switch (program->phase)
    {
    case 0:
        Paint(program, context);
        CHECK(mwinRequestKeepAwake(context, program->window, true, nullptr) == mwin_success &&
                  mwinRequestClipboardWrite(context, program->window, TEXT, strlen(TEXT),
                                            nullptr) == mwin_success,
              "keeping awake and a write asked for");
        break;
    case 1:
        CHECK(outcomes[0] == mwin_outcomeDone && outcomes[1] == mwin_outcomeDone,
              "keeping awake and the write done");
        CHECK((WindowFlags(program, context) & KEEP_SCREEN_ON) != 0 &&
                  StateOf(program, context).awake,
              "kept awake: the window's flag");
        CHECK(mwinRequestClipboardRead(context, program->window, &s_read) == mwin_success,
              "a read asked for");
        break;
    case 2:
        CHECK(outcomes[0] == mwin_outcomeDone, "the read done");
        CHECK(ReadBack(context, TEXT), "the clipboard's text read back the same");
        WriteRead(program, context, LIMIT_TEXT);
        CHECK(mwinRequestKeepAwake(context, program->window, false, nullptr) == mwin_success &&
                  mwinRequestRevealFile(context, program->window, "/sdcard", 7, nullptr) ==
                      mwin_success,
              "no longer awake, and a file shown, asked for");
        CHECK(mwinRequestClipboardReadData(context, program->window, "image/png", 9, nullptr) ==
                      mwin_success &&
                  mwinRequestPrimaryRead(context, program->window, nullptr) == mwin_success,
              "data and the primary selection asked for");
        {
            mwinMessageBoxDef box = mwinDefaultMessageBoxDef();
            CHECK(mwinShowMessageBox(&box, nullptr) == mwin_errorUnsupported,
                  "no message box to wait for");
        }
        break;
    case 3:
        CHECK(outcomes[0] == mwin_outcomeDone && outcomes[1] == mwin_outcomeDone &&
                  ReadBack(context, LIMIT_TEXT),
              "ASCII at the limit read back the same");
        CHECK(outcomes[2] == mwin_outcomeDone && outcomes[3] == mwin_outcomeUnsupported,
              "awake cleared; no file manager");
        CHECK(outcomes[4] == mwin_outcomeUnsupported && outcomes[5] == mwin_outcomeUnsupported,
              "no clipboard data, no primary selection");
        // Read while the program has the focus: Android refuses it later.
        WriteRead(program, context, "");
        CHECK((WindowFlags(program, context) & KEEP_SCREEN_ON) == 0 &&
                  !StateOf(program, context).awake,
              "the window's flag cleared");
        {
            const char url[] = "https://example.com/maul";
            CHECK(mwinRequestKeepAwake(context, program->window, true, nullptr) == mwin_success &&
                      mwinRequestOpenUrl(context, program->window, url, sizeof(url) - 1, nullptr) ==
                          mwin_success,
                  "awake again, and an address opened");
        }
        break;
    case 4:
        CHECK(outcomes[0] == mwin_outcomeDone && outcomes[1] == mwin_outcomeDone &&
                  ReadBack(context, ""),
              "empty text read back empty");
        CHECK(outcomes[3] == mwin_outcomeDone, "the address opened in the browser");
        printf("adb: am start -n " MWIN_TEST_PACKAGE "/maul.window.Activity\n");
        break;
    case 5:
    {
        // Turned the other way: the activity is made anew and joins.
        mwinWindowState state = StateOf(program, context);
        program->wasPortrait = state.pixelSize.height > state.pixelSize.width;
        printf("adb: settings put system accelerometer_rotation 0\n");
        printf("adb: settings put system user_rotation %d\n", program->wasPortrait ? 1 : 0);
        break;
    }
    case 6:
        CHECK((WindowFlags(program, context) & KEEP_SCREEN_ON) != 0,
              "the activity made anew kept awake too");
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
    def.context.limits.clipboardBytes = sizeof(LIMIT_TEXT) - 1;
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    return def;
}
