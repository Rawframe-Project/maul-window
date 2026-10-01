// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs on Android, in the emulator (tools/run_android_app.sh):
// saving and choosing a folder are unsupported; an open dialog shows the
// system's picker; a second dialog meanwhile supersedes it, its picker
// taking the first's place, a late answer under the first's number is
// not the second's, and Back closes it, cancelling. Then the picker's result is handed to
// the activity as the picker hands it (onActivityResult), with two documents the test makes in
// Downloads through MediaStore: their copies, under the names their provider gives, read back the
// same, a large one copied over several frames.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/dialog.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define LAST_PHASE  4
#define LARGE_BYTES (20u << 20)
#define SMALL       "the first document"

// Documents.REQUEST with the number of the test's third dialog, and
// Activity.RESULT_OK.
#define PICKER          (0x4d00 | 3)
#define RESULT_OK       -1
#define RESULT_CANCELED 0

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    int completions;
    mwinOutcome outcomes[4];
    mwinRequestId requests[4];
    bool suspended;
    bool resumed;
    // Whether a frame saw the large document's copy part done (the
    // copies' place, mwin-0026: the cache's maul-documents, a folder per
    // dialog and per document).
    bool partial;
    // The activity, kept while it shows: the window's handles have none
    // once its surface went.
    ANativeActivity* activity;
    // The two documents (global references to their addresses), and
    // their names.
    jobject documents[2];
    char names[2][64];
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
            program->requests[program->completions] = event.data.completion.request;
            program->outcomes[program->completions++] = event.data.completion.outcome;
        }
        program->suspended |= event.type == mwin_eventSuspending;
        program->resumed |= event.type == mwin_eventResumed;
    }
}

static ANativeActivity* ActivityOf(const Program* program, mwinContext* context)
{
    mwinNativeHandles handles = {0};
    (void)mwinGetNativeHandles(context, program->window, &handles);
    return handles.handles.android.activity;
}

static jobject Resolver(JNIEnv* env, ANativeActivity* activity)
{
    jclass type = (*env)->GetObjectClass(env, activity->clazz);
    jobject resolver =
        (*env)->CallObjectMethod(env, activity->clazz,
                                 (*env)->GetMethodID(env, type, "getContentResolver",
                                                     "()Landroid/content/ContentResolver;"));
    (*env)->DeleteLocalRef(env, type);
    return resolver;
}

// A byte of the large document.
static uint8_t LargeByte(size_t at)
{
    return (uint8_t)(at * 7u + at / 4099u);
}

// Makes a document in Downloads through MediaStore, its bytes written
// through its descriptor: its address as a global reference, or null.
static jobject MakeDocument(JNIEnv* env, ANativeActivity* activity, const char* name, bool large)
{
    jobject resolver = Resolver(env, activity);
    jclass resolvers = (*env)->GetObjectClass(env, resolver);
    jclass values = (*env)->FindClass(env, "android/content/ContentValues");
    jobject value =
        (*env)->NewObject(env, values, (*env)->GetMethodID(env, values, "<init>", "()V"));
    jstring key = (*env)->NewStringUTF(env, "_display_name");
    jstring text = (*env)->NewStringUTF(env, name);
    (*env)->CallVoidMethod(
        env, value,
        (*env)->GetMethodID(env, values, "put", "(Ljava/lang/String;Ljava/lang/String;)V"), key,
        text);
    jclass downloads = (*env)->FindClass(env, "android/provider/MediaStore$Downloads");
    jobject collection = (*env)->GetStaticObjectField(
        env, downloads,
        (*env)->GetStaticFieldID(env, downloads, "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;"));
    jobject address = (*env)->CallObjectMethod(
        env, resolver,
        (*env)->GetMethodID(env, resolvers, "insert",
                            "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;"),
        collection, value);
    jstring mode = (*env)->NewStringUTF(env, "w");
    jobject descriptor =
        address != nullptr
            ? (*env)->CallObjectMethod(
                  env, resolver,
                  (*env)->GetMethodID(
                      env, resolvers, "openFileDescriptor",
                      "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;"),
                  address, mode)
            : nullptr;
    bool written = false;
    if (descriptor != nullptr && !(*env)->ExceptionCheck(env))
    {
        jclass descriptors = (*env)->GetObjectClass(env, descriptor);
        int fd = (*env)->CallIntMethod(env, descriptor,
                                       (*env)->GetMethodID(env, descriptors, "detachFd", "()I"));
        written = true;
        if (large)
        {
            uint8_t block[4096];
            for (size_t at = 0; at < LARGE_BYTES && written; at += sizeof(block))
            {
                for (size_t i = 0; i < sizeof(block); i++)
                {
                    block[i] = LargeByte(at + i);
                }
                written = write(fd, block, sizeof(block)) == (ssize_t)sizeof(block);
            }
        }
        else
        {
            written = write(fd, SMALL, strlen(SMALL)) == (ssize_t)strlen(SMALL);
        }
        written = close(fd) == 0 && written;
        (*env)->DeleteLocalRef(env, descriptors);
    }
    (*env)->ExceptionClear(env);
    jobject kept = written ? (*env)->NewGlobalRef(env, address) : nullptr;
    jobject locals[] = {resolver,  resolvers,  values,  value, key,       text,
                        downloads, collection, address, mode,  descriptor};
    for (size_t i = 0; i < sizeof(locals) / sizeof(locals[0]); i++)
    {
        (*env)->DeleteLocalRef(env, locals[i]);
    }
    return kept;
}

// Deletes a document the test made.
static void DeleteDocument(JNIEnv* env, ANativeActivity* activity, jobject address)
{
    jobject resolver = Resolver(env, activity);
    jclass resolvers = (*env)->GetObjectClass(env, resolver);
    (void)(*env)->CallIntMethod(
        env, resolver,
        (*env)->GetMethodID(env, resolvers, "delete",
                            "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I"),
        address, nullptr, nullptr);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, resolvers);
    (*env)->DeleteLocalRef(env, resolver);
}

// Hands the activity a picker's result, as Android does.
static void Result(const Program* program, jint request, jint code, jobject intent)
{
    JNIEnv* env = program->activity->env;
    jclass activities = (*env)->GetObjectClass(env, program->activity->clazz);
    (*env)->CallVoidMethod(
        env, program->activity->clazz,
        (*env)->GetMethodID(env, activities, "onActivityResult", "(IILandroid/content/Intent;)V"),
        request, code, intent);
    CHECK(!(*env)->ExceptionCheck(env), "the result handed over");
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activities);
}

// Hands the activity the picker's result, the two documents chosen.
static void Choose(const Program* program)
{
    ANativeActivity* activity = program->activity;
    JNIEnv* env = activity->env;
    jclass clips = (*env)->FindClass(env, "android/content/ClipData");
    jclass items = (*env)->FindClass(env, "android/content/ClipData$Item");
    jmethodID item = (*env)->GetMethodID(env, items, "<init>", "(Landroid/net/Uri;)V");
    jobject first = (*env)->NewObject(env, items, item, program->documents[0]);
    jobject second = (*env)->NewObject(env, items, item, program->documents[1]);
    jstring label = (*env)->NewStringUTF(env, "documents");
    jclass strings = (*env)->FindClass(env, "java/lang/String");
    jobjectArray types = (*env)->NewObjectArray(env, 1, strings, nullptr);
    (*env)->SetObjectArrayElement(env, types, 0, (*env)->NewStringUTF(env, "*/*"));
    jobject clip = (*env)->NewObject(
        env, clips,
        (*env)->GetMethodID(
            env, clips, "<init>",
            "(Ljava/lang/CharSequence;[Ljava/lang/String;Landroid/content/ClipData$Item;)V"),
        label, types, first);
    (*env)->CallVoidMethod(
        env, clip, (*env)->GetMethodID(env, clips, "addItem", "(Landroid/content/ClipData$Item;)V"),
        second);
    jclass intents = (*env)->FindClass(env, "android/content/Intent");
    jobject intent =
        (*env)->NewObject(env, intents, (*env)->GetMethodID(env, intents, "<init>", "()V"));
    (*env)->CallVoidMethod(
        env, intent,
        (*env)->GetMethodID(env, intents, "setClipData", "(Landroid/content/ClipData;)V"), clip);
    Result(program, PICKER, RESULT_OK, intent);
    jobject locals[] = {clips, items, first, second, label, strings, types, clip, intents, intent};
    for (size_t i = 0; i < sizeof(locals) / sizeof(locals[0]); i++)
    {
        (*env)->DeleteLocalRef(env, locals[i]);
    }
}

static mwinResult Ask(Program* program, mwinContext* context, mwinDialogKind kind)
{
    static const mwinFileFilter filters[] = {{"Text", 4, "txt;md", 6}, {"Data", 4, "bin", 3}};
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = kind;
    def.filters = filters;
    def.filterCount = 2;
    return mwinRequestFileDialog(context, program->window, &def, nullptr);
}

// Whether a file holds what the document made under a name holds.
static bool SameAs(const char* path, bool large)
{
    FILE* file = fopen(path, "rb");
    if (file == nullptr)
    {
        return false;
    }
    bool same = true;
    size_t size = 0;
    uint8_t block[4096];
    for (size_t got = fread(block, 1, sizeof(block), file); got > 0 && same;
         got = fread(block, 1, sizeof(block), file))
    {
        for (size_t i = 0; i < got && same; i++)
        {
            same = large ? block[i] == LargeByte(size + i)
                         : size + i < strlen(SMALL) && block[i] == (uint8_t)SMALL[size + i];
        }
        size += got;
    }
    (void)fclose(file);
    return same && size == (large ? LARGE_BYTES : strlen(SMALL));
}

static void CheckCopies(Program* program, mwinContext* context)
{
    char paths[2048];
    size_t length = 0;
    uint32_t count = 0;
    CHECK(mwinGetDialogFiles(context, program->requests[0], paths, sizeof(paths), &length,
                             &count) == mwin_success &&
              count == 2,
          "two paths");
    const char* path = paths;
    for (uint32_t i = 0; i < count && i < 2; i++)
    {
        size_t name = strlen(program->names[i]);
        size_t size = strlen(path);
        CHECK(path[0] == '/' && size > name && strcmp(path + size - name, program->names[i]) == 0,
              "each copy under its document's name");
        CHECK(SameAs(path, i == 1), "each copy the same as its document");
        path += size + 1;
    }
    CHECK(program->partial, "the large document copied over several frames");
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
        return program->completions >= 2 && program->suspended;
    case 2:
        return program->completions >= 1 && program->resumed && state.focused;
    case 3:
        return program->suspended;
    case 4:
    {
        char path[256];
        (void)snprintf(path, sizeof(path),
                       "/data/data/" MWIN_TEST_PACKAGE "/cache/maul-documents/3/1/%s",
                       program->names[1]);
        struct stat status;
        program->partial |=
            stat(path, &status) == 0 && status.st_size > 0 && status.st_size < (off_t)LARGE_BYTES;
        return program->completions >= 1;
    }
    default:
        return true;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    ANativeActivity* activity = ActivityOf(program, context);
    switch (program->phase)
    {
    case 0:
        (void)snprintf(program->names[0], sizeof(program->names[0]), "maul-a-%d.txt",
                       (int)getpid());
        (void)snprintf(program->names[1], sizeof(program->names[1]), "maul-b-%d.bin",
                       (int)getpid());
        program->activity = activity;
        program->documents[0] = MakeDocument(activity->env, activity, program->names[0], false);
        program->documents[1] = MakeDocument(activity->env, activity, program->names[1], true);
        CHECK(program->documents[0] != nullptr && program->documents[1] != nullptr,
              "two documents made");
        CHECK(Ask(program, context, mwin_dialogSave) == mwin_success &&
                  Ask(program, context, mwin_dialogFolder) == mwin_success &&
                  Ask(program, context, mwin_dialogOpenMany) == mwin_success,
              "saving, a folder and opening asked for");
        break;
    case 1:
        CHECK(program->outcomes[0] == mwin_outcomeUnsupported &&
                  program->outcomes[1] == mwin_outcomeUnsupported,
              "no saving or folders");
        CHECK(Ask(program, context, mwin_dialogOpen) == mwin_success, "a second dialog asked for");
        // The first picker's answer, come late: not the second's.
        Result(program, 0x4d00 | 1, RESULT_CANCELED, nullptr);
        Collect(program, context);
        CHECK(program->completions == 3 && program->outcomes[2] == mwin_outcomeSuperseded,
              "the first superseded; its late answer not the second's");
        printf("adb: input keyevent KEYCODE_BACK\n");
        break;
    case 2:
        CHECK(program->outcomes[0] == mwin_outcomeCancelled, "Back cancels the second");
        program->suspended = false;
        program->activity = activity;
        CHECK(Ask(program, context, mwin_dialogOpenMany) == mwin_success, "opening again");
        break;
    case 3:
        // The picker shows: its result comes as the picker gives it, and
        // the picker itself goes with Back (its own result unheard).
        Choose(program);
        printf("adb: input keyevent KEYCODE_BACK\n");
        break;
    case 4:
        CHECK(program->outcomes[0] == mwin_outcomeDone, "the documents chosen");
        CheckCopies(program, context);
        for (int i = 0; i < 2; i++)
        {
            DeleteDocument(program->activity->env, program->activity, program->documents[i]);
        }
        break;
    default:
        break;
    }
    program->phase += 1;
    program->completions = 0;
    program->startNs = NowNs();
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
    if (program->phase == 0 && program->startNs != 0)
    {
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
