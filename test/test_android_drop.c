// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drag and drop on Android, in the emulator (tools/run_android_app.sh):
// a drag the test starts from its own window, under a finger the runner
// puts down, moves and lifts, carrying a document it made in Downloads
// through MediaStore and text, goes through Android's own drag and drop:
// the window hears it enter and move with both contents, and the drop at
// the place the finger lifted, with the document copied under its name
// and the text.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/drop.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define LAST_PHASE  2
#define CONTENT     "the dropped document"
#define WORDS       "dropped w\xc3\xb6rds"
#define MOVE_BY     200

// View.DRAG_FLAG_GLOBAL and DRAG_FLAG_GLOBAL_URI_READ.
#define DRAG_FLAGS (256 | 1)

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    ANativeActivity* activity;
    float scale;
    int x;
    int y;
    bool touched;
    bool entered;
    bool moved;
    bool left;
    mwinDragContents contents;
    bool dropped;
    mwinDropEvent drop;
    jobject document;
    char name[64];
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
        program->touched |= event.type == mwin_eventTouchDown;
        program->entered |= event.type == mwin_eventDragEntered;
        program->moved |= event.type == mwin_eventDragMoved;
        program->left |= event.type == mwin_eventDragLeft;
        if (event.type == mwin_eventDragEntered)
        {
            program->contents = event.data.drag.contents;
        }
        if (event.type == mwin_eventDropped)
        {
            program->dropped = true;
            program->drop = event.data.drop;
        }
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

// Makes a document in Downloads through MediaStore, its bytes written
// through its descriptor: its address as a global reference, or null.
static jobject MakeDocument(JNIEnv* env, ANativeActivity* activity, const char* name)
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
        written = write(fd, CONTENT, strlen(CONTENT)) == (ssize_t)strlen(CONTENT);
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

// Starts a drag from the activity's view, under the finger that is down,
// carrying the document and text.
static bool StartDrag(const Program* program)
{
    JNIEnv* env = program->activity->env;
    jobject window =
        CallObject(env, program->activity->clazz, "getWindow", "()Landroid/view/Window;");
    jobject decor = CallObject(env, window, "getDecorView", "()Landroid/view/View;");
    jobject view = CallObject(env, decor, "findFocus", "()Landroid/view/View;");
    jclass clips = (*env)->FindClass(env, "android/content/ClipData");
    jclass items = (*env)->FindClass(env, "android/content/ClipData$Item");
    jobject document = (*env)->NewObject(
        env, items, (*env)->GetMethodID(env, items, "<init>", "(Landroid/net/Uri;)V"),
        program->document);
    jstring words = (*env)->NewStringUTF(env, WORDS);
    jobject text = (*env)->NewObject(
        env, items, (*env)->GetMethodID(env, items, "<init>", "(Ljava/lang/CharSequence;)V"),
        words);
    jclass strings = (*env)->FindClass(env, "java/lang/String");
    jobjectArray types = (*env)->NewObjectArray(env, 2, strings, nullptr);
    jstring plain = (*env)->NewStringUTF(env, "text/plain");
    jstring octets = (*env)->NewStringUTF(env, "application/octet-stream");
    (*env)->SetObjectArrayElement(env, types, 0, octets);
    (*env)->SetObjectArrayElement(env, types, 1, plain);
    jstring label = (*env)->NewStringUTF(env, "drag");
    jobject clip =
        (*env)->NewObject(env, clips,
                          (*env)->GetMethodID(env, clips, "<init>",
                                              "(Ljava/lang/CharSequence;[Ljava/lang/String;"
                                              "Landroid/content/ClipData$Item;)V"),
                          label, types, document);
    (*env)->CallVoidMethod(
        env, clip, (*env)->GetMethodID(env, clips, "addItem", "(Landroid/content/ClipData$Item;)V"),
        text);
    jclass shadows = (*env)->FindClass(env, "android/view/View$DragShadowBuilder");
    jobject shadow = (*env)->NewObject(
        env, shadows, (*env)->GetMethodID(env, shadows, "<init>", "(Landroid/view/View;)V"), view);
    jclass views = (*env)->FindClass(env, "android/view/View");
    jboolean started = (*env)->CallBooleanMethod(
        env, view,
        (*env)->GetMethodID(env, views, "startDragAndDrop",
                            "(Landroid/content/ClipData;Landroid/view/View$DragShadowBuilder;"
                            "Ljava/lang/Object;I)Z"),
        clip, shadow, nullptr, DRAG_FLAGS);
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    jobject locals[] = {window, decor, view,   clips, items, document, words,  text, strings,
                        types,  plain, octets, label, clip,  shadows,  shadow, views};
    for (size_t i = 0; i < sizeof(locals) / sizeof(locals[0]); i++)
    {
        (*env)->DeleteLocalRef(env, locals[i]);
    }
    return started && !thrown;
}

static void CheckDrop(const Program* program, mwinContext* context)
{
    const mwinDropEvent* drop = &program->drop;
    CHECK(program->entered && program->moved && !program->left,
          "the drag entered and moved, and did not leave");
    CHECK(program->contents == (mwin_dragFiles | mwin_dragText), "files and text dragged");
    CHECK(fabsf(drop->position.x - (float)(program->x + MOVE_BY) / program->scale) < 2.0f &&
              fabsf(drop->position.y - (float)(program->y + MOVE_BY) / program->scale) < 2.0f,
          "dropped where the finger lifted, in logical units");
    CHECK(drop->fileCount == 1 && drop->textLength == strlen(WORDS) && !drop->truncated,
          "one file and the text");
    char paths[1024] = {0};
    size_t length = 0;
    CHECK(mwinGetDroppedFiles(context, drop->drop, paths, sizeof(paths), &length) == mwin_success,
          "the paths copied out");
    size_t name = strlen(program->name);
    size_t size = strlen(paths);
    CHECK(size > name && strcmp(paths + size - name, program->name) == 0,
          "the copy under the document's name");
    char content[64] = {0};
    FILE* file = fopen(paths, "rb");
    size_t got = file != nullptr ? fread(content, 1, sizeof(content) - 1, file) : 0;
    if (file != nullptr)
    {
        (void)fclose(file);
    }
    CHECK(got == strlen(CONTENT) && memcmp(content, CONTENT, got) == 0,
          "the copy the same as the document");
    char text[64] = {0};
    CHECK(mwinGetDroppedText(context, drop->drop, text, sizeof(text), &length) == mwin_success &&
              length == strlen(WORDS) && memcmp(text, WORDS, length) == 0,
          "the text");
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
        return program->touched;
    case 2:
        return program->dropped;
    default:
        return true;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
    {
        mwinWindowState state = {0};
        (void)mwinGetWindowState(context, program->window, &state);
        program->activity = ActivityOf(program, context);
        program->scale = state.scale;
        program->x = (int)state.pixelSize.width / 3;
        program->y = (int)state.pixelSize.height / 3;
        (void)snprintf(program->name, sizeof(program->name), "maul-drop-%d.txt", (int)getpid());
        program->document = MakeDocument(program->activity->env, program->activity, program->name);
        CHECK(program->document != nullptr, "a document made");
        printf("adb: input motionevent DOWN %d %d\n", program->x, program->y);
        break;
    }
    case 1:
        CHECK(StartDrag(program), "a drag started under the finger");
        printf("adb: input motionevent MOVE %d %d\n", program->x + MOVE_BY / 2,
               program->y + MOVE_BY / 2);
        printf("adb: input motionevent MOVE %d %d\n", program->x + MOVE_BY, program->y + MOVE_BY);
        printf("adb: input motionevent UP %d %d\n", program->x + MOVE_BY, program->y + MOVE_BY);
        break;
    case 2:
        CheckDrop(program, context);
        DeleteDocument(program->activity->env, program->activity, program->document);
        break;
    default:
        break;
    }
    program->phase += 1;
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
    if (program->phase == 0)
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
