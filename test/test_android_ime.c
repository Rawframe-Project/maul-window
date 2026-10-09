// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods, the on-screen keyboard and the insets on Android, in the
// emulator (tools/run_android_app.sh). The window drawn behind the bars
// has a safe area; the keyboard, asked for, covers the bottom of the
// window and goes when asked to. The test calls the activity's input
// connection as an input method would (through JNI, the user's typing
// being out of its reach): committed text as text, compositions told
// while the window accepts text, a newline as the Enter key, deletions
// outside a composition and sent keys as keys, with their modifiers; a
// composition dropped when the window stops accepting text, not
// committed when the input method closes its old connection, and none
// told while it does not; each purpose's input type, nothing corrected
// or completed. Insets told again alike change neither the safe area
// nor the keyboard.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/input.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define RECORDS     64
#define LAST_PHASE  13

// Android's input types (android.text.InputType).
#define TYPE_CLASS_TEXT               0x1
#define TYPE_CLASS_NUMBER             0x2
#define TYPE_TEXT_VARIATION_EMAIL     0x20
#define TYPE_TEXT_FLAG_NO_SUGGESTIONS 0x80000
#define KEYCODE_DEL                   67

// A record kept past its frame, with its text copied.
typedef struct Record
{
    mwinEventType type;
    mwinKeyCode code;
    mwinModifiers modifiers;
    char text[32];
    int32_t caret;
    uint32_t selectionStart;
    uint32_t selectionEnd;
    uint32_t segments;
} Record;

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    int completions;
    Record records[RECORDS];
    int recordCount;
    // The connection a composition was left in (a global reference).
    jobject composing;
    // The safe area's and the keyboard's changes told, and when the last
    // came.
    int safeAreas;
    int keyboards;
    uint64_t insetsNs;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void Keep(Program* program, const mwinEvent* event)
{
    if (program->recordCount >= RECORDS)
    {
        return;
    }
    Record* record = &program->records[program->recordCount++];
    *record = (Record){.type = event->type};
    const char* text = nullptr;
    uint32_t length = 0;
    if (event->type == mwin_eventKeyDown || event->type == mwin_eventKeyUp)
    {
        record->code = event->data.key.code;
        record->modifiers = event->data.key.modifiers;
    }
    else if (event->type == mwin_eventTextInput)
    {
        text = event->data.text.text;
        length = event->data.text.length;
    }
    else
    {
        const mwinPreeditEvent* preedit = &event->data.preedit;
        text = preedit->text;
        length = preedit->length;
        record->caret = preedit->caret;
        record->selectionStart = preedit->selectionStart;
        record->selectionEnd = preedit->selectionEnd;
        record->segments = preedit->segmentCount;
    }
    if (text != nullptr && length < sizeof(record->text))
    {
        memcpy(record->text, text, length);
    }
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        program->completions += event.type == mwin_eventRequestCompleted;
        program->safeAreas += event.type == mwin_eventSafeAreaChanged;
        program->keyboards += event.type == mwin_eventVirtualKeyboardChanged;
        if (event.type == mwin_eventSafeAreaChanged ||
            event.type == mwin_eventVirtualKeyboardChanged)
        {
            program->insetsNs = NowNs();
        }
        if (event.type == mwin_eventKeyDown || event.type == mwin_eventKeyUp ||
            event.type == mwin_eventTextInput || event.type == mwin_eventImePreedit)
        {
            Keep(program, &event);
        }
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

// The activity's focused view's input connection, as an input method
// gets it, and the input type it gives.
static jobject Connection(JNIEnv* env, ANativeActivity* activity, jint* typeOut)
{
    jobject window = CallObject(env, activity->clazz, "getWindow", "()Landroid/view/Window;");
    jobject decor = CallObject(env, window, "getDecorView", "()Landroid/view/View;");
    jobject field = CallObject(env, decor, "findFocus", "()Landroid/view/View;");
    jclass infos = (*env)->FindClass(env, "android/view/inputmethod/EditorInfo");
    jobject info = (*env)->NewObject(env, infos, (*env)->GetMethodID(env, infos, "<init>", "()V"));
    jobject connection = nullptr;
    if (field != nullptr)
    {
        jclass views = (*env)->FindClass(env, "android/view/View");
        jmethodID create = (*env)->GetMethodID(
            env, views, "onCreateInputConnection",
            "(Landroid/view/inputmethod/EditorInfo;)Landroid/view/inputmethod/InputConnection;");
        connection = (*env)->CallObjectMethod(env, field, create, info);
        (*env)->DeleteLocalRef(env, views);
    }
    *typeOut = (*env)->GetIntField(env, info, (*env)->GetFieldID(env, infos, "inputType", "I"));
    (*env)->DeleteLocalRef(env, window);
    (*env)->DeleteLocalRef(env, decor);
    (*env)->DeleteLocalRef(env, field);
    (*env)->DeleteLocalRef(env, infos);
    (*env)->DeleteLocalRef(env, info);
    return connection;
}

// The input method's calls on a connection.
static void Text(JNIEnv* env, jobject connection, const char* name, const char* text)
{
    jclass type = (*env)->FindClass(env, "android/view/inputmethod/InputConnection");
    jmethodID method = (*env)->GetMethodID(env, type, name, "(Ljava/lang/CharSequence;I)Z");
    jstring string = (*env)->NewStringUTF(env, text);
    (void)(*env)->CallBooleanMethod(env, connection, method, string, 1);
    (*env)->DeleteLocalRef(env, string);
    (*env)->DeleteLocalRef(env, type);
}

static void Delete(JNIEnv* env, jobject connection, jint before, jint after)
{
    jclass type = (*env)->FindClass(env, "android/view/inputmethod/InputConnection");
    (void)(*env)->CallBooleanMethod(
        env, connection, (*env)->GetMethodID(env, type, "deleteSurroundingText", "(II)Z"), before,
        after);
    (*env)->DeleteLocalRef(env, type);
}

static void SendKey(JNIEnv* env, jobject connection, jint action, jint code, jint meta)
{
    jclass events = (*env)->FindClass(env, "android/view/KeyEvent");
    jobject event =
        (*env)->NewObject(env, events, (*env)->GetMethodID(env, events, "<init>", "(JJIIII)V"),
                          (jlong)0, (jlong)0, action, code, 0, meta);
    jclass type = (*env)->FindClass(env, "android/view/inputmethod/InputConnection");
    (void)(*env)->CallBooleanMethod(
        env, connection,
        (*env)->GetMethodID(env, type, "sendKeyEvent", "(Landroid/view/KeyEvent;)Z"), event);
    (*env)->DeleteLocalRef(env, type);
    (*env)->DeleteLocalRef(env, event);
    (*env)->DeleteLocalRef(env, events);
}

static void Finish(JNIEnv* env, jobject connection)
{
    jclass type = (*env)->FindClass(env, "android/view/inputmethod/InputConnection");
    (void)(*env)->CallBooleanMethod(env, connection,
                                    (*env)->GetMethodID(env, type, "finishComposingText", "()Z"));
    (*env)->DeleteLocalRef(env, type);
}

static void EditorAction(JNIEnv* env, jobject connection)
{
    jclass type = (*env)->FindClass(env, "android/view/inputmethod/InputConnection");
    (void)(*env)->CallBooleanMethod(
        env, connection, (*env)->GetMethodID(env, type, "performEditorAction", "(I)Z"), 6);
    (*env)->DeleteLocalRef(env, type);
}

static bool IsText(const Record* record, const char* text)
{
    return record->type == mwin_eventTextInput && strcmp(record->text, text) == 0;
}

static bool IsPreedit(const Record* record, const char* text, int32_t caret)
{
    return record->type == mwin_eventImePreedit && strcmp(record->text, text) == 0 &&
           record->caret == caret;
}

static bool IsKey(const Record* record, mwinEventType type, mwinKeyCode code)
{
    return record->type == type && record->code == code;
}

// A key's press and release at a place of the records.
static bool IsTap(const Program* program, int at, mwinKeyCode code)
{
    return at + 1 < program->recordCount && IsKey(&program->records[at], mwin_eventKeyDown, code) &&
           IsKey(&program->records[at + 1], mwin_eventKeyUp, code);
}

static bool Ready(const Program* program, mwinContext* context)
{
    mwinWindowState state = StateOf(program, context);
    switch (program->phase)
    {
    case 0:
        return program->shown && state.safeArea.top > 0.0f;
    case 1:
        return state.virtualKeyboard.height > 0.0f;
    case 7:
    case 9:
    case 10:
        return program->completions >= 1;
    case 8:
        return state.virtualKeyboard.height == 0.0f;
    // Android's own insets settled, half a second without a change.
    case 11:
        return state.virtualKeyboard.height == 0.0f && NowNs() - program->insetsNs > 500000000u;
    case 12:
        return program->safeAreas >= 1 && program->keyboards >= 1;
    default:
        return true;
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

static void CheckKeyboard(const Program* program, mwinContext* context)
{
    mwinWindowState state = StateOf(program, context);
    mwinRect covered = state.virtualKeyboard;
    CHECK(fabsf(covered.y + covered.height - state.size.height) < 1.0f &&
              fabsf(covered.width - state.size.width) < 1.0f && covered.x == 0.0f,
          "the keyboard covers the window's bottom");
}

static void CheckKeys(const Program* program)
{
    CHECK(program->recordCount == 14, "fourteen records");
    if (program->recordCount != 14)
    {
        return;
    }
    CHECK(IsTap(program, 0, mwin_codeBackspace) && IsTap(program, 2, mwin_codeBackspace) &&
              IsTap(program, 4, mwin_codeDelete),
          "a deletion outside a composition as Backspace and Delete");
    CHECK(IsText(&program->records[6], "a") && IsTap(program, 7, mwin_codeEnter) &&
              IsText(&program->records[9], "b"),
          "a newline in committed text as Enter");
    CHECK(IsTap(program, 10, mwin_codeBackspace), "a key the input method sends");
    CHECK(program->records[10].modifiers ==
                  (mwin_modControl | mwin_modAlt | mwin_modCapsLock | mwin_modNumLock) &&
              program->records[11].modifiers == 0,
          "its modifiers from its meta state: Control, Alt and both locks, then none");
    CHECK(IsTap(program, 12, mwin_codeEnter), "the editor's action as Enter");
}

static void CheckType(const Program* program, mwinContext* context, jint expected, const char* what)
{
    ANativeActivity* activity = ActivityOf(program, context);
    JNIEnv* env = activity->env;
    jint type = 0;
    jobject connection = Connection(env, activity, &type);
    CHECK(connection != nullptr && type == expected, what);
    (*env)->DeleteLocalRef(env, connection);
}

// Tells the activity's insets, as Android does: a safe area and a
// keyboard of the test's own.
static void TellInsets(Program* program, const ANativeActivity* activity)
{
    JNIEnv* env = activity->env;
    program->safeAreas = 0;
    program->keyboards = 0;
    jclass activities = (*env)->GetObjectClass(env, activity->clazz);
    jlong handle = (*env)->GetStaticLongField(
        env, activities, (*env)->GetStaticFieldID(env, activities, "program", "J"));
    (*env)->CallStaticVoidMethod(
        env, activities, (*env)->GetStaticMethodID(env, activities, "nativeInsets", "(JIIIII)V"),
        handle, 0, 10, 0, 20, 30);
    CHECK(!(*env)->ExceptionCheck(env), "the insets told");
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activities);
}

// A composition record is a notification: a later one replaces one still
// waiting, so the test makes one composition call a frame.
static void Advance(Program* program, mwinContext* context)
{
    ANativeActivity* activity = ActivityOf(program, context);
    JNIEnv* env = activity != nullptr ? activity->env : nullptr;
    jint type = 0;
    jobject connection = env != nullptr ? Connection(env, activity, &type) : nullptr;
    const Record* records = program->records;
    int count = program->recordCount;
    mwinRect caret = {10.0f, 10.0f, 1.0f, 20.0f};
    switch (program->phase)
    {
    case 0:
        Paint(program, context);
        CHECK(mwinRequestTextInput(context, program->window, true, caret, nullptr) ==
                      mwin_success &&
                  mwinRequestVirtualKeyboard(context, program->window, true, mwin_purposeText,
                                             nullptr) == mwin_success,
              "text input and the keyboard asked for");
        break;
    case 1:
        CheckKeyboard(program, context);
        CHECK(type == (TYPE_CLASS_TEXT | TYPE_TEXT_FLAG_NO_SUGGESTIONS), "text, nothing suggested");
        Text(env, connection, "commitText", "h\xc3\xa9");
        Text(env, connection, "setComposingText", "\xe3\x81\xab\xe3\x81\xbb");
        break;
    case 2:
        CHECK(count == 2 && IsText(&records[0], "h\xc3\xa9") &&
                  IsPreedit(&records[1], "\xe3\x81\xab\xe3\x81\xbb", 6) &&
                  records[1].segments == 1 && records[1].selectionStart == 6 &&
                  records[1].selectionEnd == 6,
              "committed text; a composition, its caret at its end in bytes");
        Text(env, connection, "setComposingText", "\xe6\x97\xa5\xe6\x9c\xac");
        break;
    case 3:
        CHECK(count == 1 && IsPreedit(&records[0], "\xe6\x97\xa5\xe6\x9c\xac", 6),
              "the composition converted");
        Text(env, connection, "commitText", "\xe6\x97\xa5\xe6\x9c\xac");
        break;
    case 4:
        CHECK(count == 2 && IsText(&records[0], "\xe6\x97\xa5\xe6\x9c\xac") &&
                  records[1].type == mwin_eventImePreedit && records[1].caret == -1 &&
                  !StateOf(program, context).composing,
              "committed, and the composition ended");
        Delete(env, connection, 2, 1);
        Text(env, connection, "commitText", "a\nb");
        SendKey(env, connection, 0, KEYCODE_DEL,
                AMETA_CTRL_ON | AMETA_ALT_ON | AMETA_CAPS_LOCK_ON | AMETA_NUM_LOCK_ON);
        SendKey(env, connection, 1, KEYCODE_DEL, 0);
        EditorAction(env, connection);
        break;
    case 5:
        CheckKeys(program);
        Text(env, connection, "setComposingText", "xy");
        program->composing = (*env)->NewGlobalRef(env, connection);
        break;
    case 6:
        CHECK(count == 1 && IsPreedit(&records[0], "xy", 2), "composing");
        program->completions = 0;
        CHECK(mwinRequestTextInput(context, program->window, false, caret, nullptr) == mwin_success,
              "text input stopped");
        // The input method closes its old connection, which finishes the
        // composition.
        Finish(env, program->composing);
        (*env)->DeleteGlobalRef(env, program->composing);
        break;
    case 7:
        CHECK(count == 1 && records[0].type == mwin_eventImePreedit && records[0].caret == -1 &&
                  !StateOf(program, context).composing,
              "the composition dropped when text input stops, not committed");
        // Not accepting text: no composition is told, committed text is.
        Text(env, connection, "setComposingText", "zz");
        Text(env, connection, "commitText", "zz");
        CHECK(mwinRequestVirtualKeyboard(context, program->window, false, mwin_purposeText,
                                         nullptr) == mwin_success,
              "the keyboard hidden");
        break;
    case 8:
        CHECK(count == 1 && IsText(&records[0], "zz"),
              "no composition told while not accepting text, its text committed");
        program->completions = 0;
        CHECK(mwinRequestVirtualKeyboard(context, program->window, true, mwin_purposeNumber,
                                         nullptr) == mwin_success,
              "a keyboard for a number");
        break;
    case 9:
        CheckType(program, context, 0x3002, "a number, signed and decimal");
        program->completions = 0;
        CHECK(mwinRequestVirtualKeyboard(context, program->window, true, mwin_purposeEmail,
                                         nullptr) == mwin_success,
              "a keyboard for an address");
        break;
    case 10:
        CheckType(program, context,
                  TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_EMAIL | TYPE_TEXT_FLAG_NO_SUGGESTIONS,
                  "an address");
        CHECK(mwinRequestVirtualKeyboard(context, program->window, false, mwin_purposeText,
                                         nullptr) == mwin_success,
              "the keyboard hidden again");
        break;
    case 11:
        TellInsets(program, activity);
        break;
    case 12:
        CHECK(program->safeAreas == 1 && program->keyboards == 1,
              "insets told: one change of each");
        // Again, alike, in a frame of its own: the queue merges a frame's
        // changes of one kind.
        TellInsets(program, activity);
        break;
    case 13:
        if (program->safeAreas != 0 || program->keyboards != 0)
        {
            printf("changes: %d of the safe area, %d of the keyboard\n", program->safeAreas,
                   program->keyboards);
        }
        CHECK(program->safeAreas == 0 && program->keyboards == 0, "insets told alike: no change");
        break;
    default:
        break;
    }
    if (connection != nullptr)
    {
        (*env)->DeleteLocalRef(env, connection);
    }
    program->phase += 1;
    program->recordCount = 0;
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
