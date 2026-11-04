// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drag and drop on Android, in the emulator (tools/run_android_app.sh):
// a drag the test starts from its own window, under a finger the runner
// puts down, moves and lifts, carrying a document it made in Downloads
// through MediaStore, the address of one deleted before the drag, and
// text, goes through Android's own drag and drop: the window hears it
// enter and move with both contents, and the drop at the place the
// finger lifted, with the document copied under its name, the deleted
// one left out and the drop marked truncated, and the text; a drag of
// text alone after it is dropped with its text, and not marked
// truncated; and a drag of a document whose provider (the test's
// Stalls) sends its first bytes and then nothing, and text, is dropped
// once the copy has waited five seconds for more, with its text, the
// document left out and the drop marked truncated.

#include "test_android_app.h"
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
#define LAST_PHASE  6
#define CONTENT     "the dropped document"
#define WORDS       "dropped w\xc3\xb6rds"
#define MOVE_BY     200

// View.DRAG_FLAG_GLOBAL and DRAG_FLAG_GLOBAL_URI_READ.
#define DRAG_FLAGS (256 | 1)
// The stalled document's address, and how long the copy waits for it.
#define STALLED  "content://" MWIN_TEST_PACKAGE ".stalls/stalled"
#define STALL_NS 5000000000ull

// What a drag carries.
enum
{
    // The document, the deleted one's address and text.
    DRAG_DOCUMENTS,
    // Text alone.
    DRAG_TEXT,
    // The stalled document and text.
    DRAG_STALLED,
};

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
    // A document's address that no longer opens, and the stalled one's.
    jobject gone;
    jobject stalled;
    // When the finger lifted to drop the stalled document, and when its
    // drop came.
    uint64_t liftedNs;
    uint64_t droppedNs;
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
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        program->touched |= event.kind == mwin_eventTouchDown;
        program->entered |= event.kind == mwin_eventDragEntered;
        program->moved |= event.kind == mwin_eventDragMoved;
        program->left |= event.kind == mwin_eventDragLeft;
        if (event.kind == mwin_eventDragEntered)
        {
            program->contents = event.data.drag.contents;
        }
        if (event.kind == mwin_eventDropped)
        {
            program->dropped = true;
            program->drop = event.data.drop;
        }
    }
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

// An address parsed, as a global reference.
static jobject Address(JNIEnv* env, const char* text)
{
    jclass uris = (*env)->FindClass(env, "android/net/Uri");
    jstring string = (*env)->NewStringUTF(env, text);
    jobject parsed = (*env)->CallStaticObjectMethod(
        env, uris,
        (*env)->GetStaticMethodID(env, uris, "parse", "(Ljava/lang/String;)Landroid/net/Uri;"),
        string);
    (*env)->ExceptionClear(env);
    jobject kept = parsed != nullptr ? (*env)->NewGlobalRef(env, parsed) : nullptr;
    (*env)->DeleteLocalRef(env, parsed);
    (*env)->DeleteLocalRef(env, string);
    (*env)->DeleteLocalRef(env, uris);
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
// carrying what the kind says.
static bool StartDrag(const Program* program, int kind)
{
    bool documents = kind != DRAG_TEXT;
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
    jobject gone = (*env)->NewObject(
        env, items, (*env)->GetMethodID(env, items, "<init>", "(Landroid/net/Uri;)V"),
        program->gone);
    jobject stalled = (*env)->NewObject(
        env, items, (*env)->GetMethodID(env, items, "<init>", "(Landroid/net/Uri;)V"),
        program->stalled);
    jstring words = (*env)->NewStringUTF(env, WORDS);
    jobject text = (*env)->NewObject(
        env, items, (*env)->GetMethodID(env, items, "<init>", "(Ljava/lang/CharSequence;)V"),
        words);
    jclass strings = (*env)->FindClass(env, "java/lang/String");
    jobjectArray types = (*env)->NewObjectArray(env, documents ? 2 : 1, strings, nullptr);
    jstring plain = (*env)->NewStringUTF(env, "text/plain");
    jstring octets = (*env)->NewStringUTF(env, "application/octet-stream");
    if (documents)
    {
        (*env)->SetObjectArrayElement(env, types, 0, octets);
    }
    (*env)->SetObjectArrayElement(env, types, documents ? 1 : 0, plain);
    jstring label = (*env)->NewStringUTF(env, "drag");
    jobject clip =
        (*env)->NewObject(env, clips,
                          (*env)->GetMethodID(env, clips, "<init>",
                                              "(Ljava/lang/CharSequence;[Ljava/lang/String;"
                                              "Landroid/content/ClipData$Item;)V"),
                          label, types,
                          kind == DRAG_DOCUMENTS ? document
                          : kind == DRAG_STALLED ? stalled
                                                 : text);
    jmethodID add =
        (*env)->GetMethodID(env, clips, "addItem", "(Landroid/content/ClipData$Item;)V");
    if (kind == DRAG_DOCUMENTS)
    {
        (*env)->CallVoidMethod(env, clip, add, gone);
    }
    if (documents)
    {
        (*env)->CallVoidMethod(env, clip, add, text);
    }
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
    jobject locals[] = {window,  decor, view,    clips,   items, document, gone,
                        stalled, words, text,    strings, types, plain,    octets,
                        label,   clip,  shadows, shadow,  views};
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
    CHECK(drop->fileCount == 1 && drop->textLength == strlen(WORDS) && drop->truncated,
          "one file, the deleted document left out and the drop marked truncated, and the text");
    char paths[1024] = {0};
    size_t length = 0;
    CHECK(mwinDrop_GetFiles(context, drop->drop, paths, sizeof(paths), &length) == mwin_success,
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
    CHECK(mwinDrop_GetText(context, drop->drop, text, sizeof(text), &length) == mwin_success &&
              length == strlen(WORDS) && memcmp(text, WORDS, length) == 0,
          "the text");
}

// The drag of text alone, after the one that left a document out.
static void CheckTextDrop(const Program* program, mwinContext* context)
{
    const mwinDropEvent* drop = &program->drop;
    CHECK(program->entered && program->contents == mwin_dragText,
          "the drag of text alone entered with text");
    CHECK(drop->fileCount == 0 && drop->textLength == strlen(WORDS) && !drop->truncated,
          "the drop of text alone: its text, no file, and not marked truncated");
    char text[64] = {0};
    size_t length = 0;
    CHECK(mwinDrop_GetText(context, drop->drop, text, sizeof(text), &length) == mwin_success &&
              length == strlen(WORDS) && memcmp(text, WORDS, length) == 0,
          "the text dropped alone");
}

// The drag of the stalled document: dropped once the copy gave up on it.
static void CheckStalledDrop(const Program* program, mwinContext* context)
{
    const mwinDropEvent* drop = &program->drop;
    CHECK(drop->fileCount == 0 && drop->textLength == strlen(WORDS) && drop->truncated,
          "the stalled document left out and the drop marked truncated, with the text");
    CHECK(program->droppedNs - program->liftedNs >= STALL_NS - STALL_NS / 10,
          "the drop told once the copy waited five seconds for the stalled document");
    char text[64] = {0};
    size_t length = 0;
    CHECK(mwinDrop_GetText(context, drop->drop, text, sizeof(text), &length) == mwin_success &&
              length == strlen(WORDS) && memcmp(text, WORDS, length) == 0,
          "the text dropped with the stalled document");
}

// The flags of a drag, cleared for the next, under a finger put down
// again.
static void Again(Program* program)
{
    program->touched = false;
    program->entered = false;
    program->moved = false;
    program->left = false;
    program->dropped = false;
    printf("adb: input motionevent DOWN %d %d\n", program->x, program->y);
}

static bool Ready(Program* program, mwinContext* context)
{
    mwinWindowState state = {0};
    (void)mwinWindow_GetState(context, program->window, &state);
    switch (program->phase)
    {
    // Focused, and with its surface: the activity is kept from here.
    case 0:
        return state.focused && HasSurface(context, program->window);
    case 1:
    case 3:
    case 5:
        return program->touched;
    case 2:
    case 4:
    case 6:
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
        (void)mwinWindow_GetState(context, program->window, &state);
        program->activity = ActivityWithSurface(context, program->window);
        CHECK(program->activity != nullptr, "the activity at the start");
        if (program->activity == nullptr)
        {
            break;
        }
        program->scale = state.scale;
        program->x = (int)state.pixelSize.width / 3;
        program->y = (int)state.pixelSize.height / 3;
        (void)snprintf(program->name, sizeof(program->name), "maul-drop-%d.txt", (int)getpid());
        program->document = MakeDocument(program->activity->env, program->activity, program->name);
        CHECK(program->document != nullptr, "a document made");
        char goneName[64];
        (void)snprintf(goneName, sizeof(goneName), "maul-gone-%d.txt", (int)getpid());
        program->gone = MakeDocument(program->activity->env, program->activity, goneName);
        CHECK(program->gone != nullptr, "a document made to delete");
        if (program->gone != nullptr)
        {
            DeleteDocument(program->activity->env, program->activity, program->gone);
        }
        program->stalled = Address(program->activity->env, STALLED);
        printf("adb: input motionevent DOWN %d %d\n", program->x, program->y);
        break;
    }
    case 1:
    case 3:
    case 5:
        CHECK(StartDrag(program, program->phase == 1   ? DRAG_DOCUMENTS
                                 : program->phase == 3 ? DRAG_TEXT
                                                       : DRAG_STALLED),
              "a drag started under the finger");
        printf("adb: input motionevent MOVE %d %d\n", program->x + MOVE_BY / 2,
               program->y + MOVE_BY / 2);
        printf("adb: input motionevent MOVE %d %d\n", program->x + MOVE_BY, program->y + MOVE_BY);
        printf("adb: input motionevent UP %d %d\n", program->x + MOVE_BY, program->y + MOVE_BY);
        program->liftedNs = NowNs();
        break;
    case 2:
        CheckDrop(program, context);
        DeleteDocument(program->activity->env, program->activity, program->document);
        // The drag of text alone.
        Again(program);
        break;
    case 4:
        CheckTextDrop(program, context);
        // The drag of the stalled document.
        Again(program);
        break;
    case 6:
        program->droppedNs = NowNs();
        CheckStalledDrop(program, context);
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
