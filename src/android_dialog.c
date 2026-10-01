// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs on Android (android.h), through the library's Java helper
// maul.window.Documents and the system's document picker.
//
// - Android's documents are content addresses, never paths, and a
//   document's descriptor cannot be opened again by its /proc/self/fd
//   path (it points into shared storage, which the application may not
//   open): each document chosen is copied into the application's cache,
//   a folder of its own per dialog and per document under the name its
//   provider gives (or "Document"), and the dialog answers with the
//   copies' paths, as iOS's picker copies its documents in. The copy
//   runs between frames, at most COPY_BYTES a frame, from a descriptor
//   that never blocks: frames go on, and the request completes once
//   every copy is whole. The copies of earlier runs go at the start;
//   the system may empty the cache in time.
// - Only opening is offered: a document the picker creates, or a
//   folder, has no path a program could write through, so saving and
//   choosing a folder are unsupported. A dialog asked for while another
//   waits supersedes it (the core answers the old one): the old picker
//   goes, any copy of its documents stops, and an answer under its
//   number is dropped.
// - Filters offer the content types Android knows of their extensions,
//   all at once; the picker shows no title and starts where it chooses.

#include "allocator.h"
#include "android.h"
#include "dialog.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COPY_BYTES  (8u << 20)
#define BLOCK_BYTES (64u << 10)
#define FOLDER      "/maul-documents"

static void JNICALL Documents(JNIEnv* env, jclass type, jlong program, jint number, jboolean chosen,
                              jintArray descriptors, jobjectArray names);

static int RemoveEntry(const char* path, const struct stat* status, int flag, struct FTW* walk)
{
    (void)status;
    (void)flag;
    (void)walk;
    (void)remove(path);
    return 0;
}

bool mwinAndroidFindDialogs(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    static const JNINativeMethod methods[] = {
        {"nativeDocuments", "(JIZ[I[Ljava/lang/String;)V", (void*)Documents},
    };
    JNIEnv* env = activity->env;
    mwinAndroidDocuments* documents = &platform->documents;
    jclass type = mwinAndroidLoadClass(env, activity, "maul.window.Documents");
    bool found = type != nullptr && (*env)->RegisterNatives(env, type, methods, 1) == JNI_OK;
    if (found)
    {
        documents->type = (*env)->NewGlobalRef(env, type);
        documents->open = (*env)->GetStaticMethodID(
            env, type, "open", "(Landroid/app/Activity;ZLjava/lang/String;II)Z");
        found = documents->type != nullptr && documents->open != nullptr;
    }
    // The cache's path, through Context.getCacheDir().
    jclass activities = (*env)->GetObjectClass(env, activity->clazz);
    jobject cache = (*env)->CallObjectMethod(
        env, activity->clazz,
        (*env)->GetMethodID(env, activities, "getCacheDir", "()Ljava/io/File;"));
    jclass files = cache != nullptr ? (*env)->GetObjectClass(env, cache) : nullptr;
    jstring path = files != nullptr
                       ? (*env)->CallObjectMethod(env, cache,
                                                  (*env)->GetMethodID(env, files, "getAbsolutePath",
                                                                      "()Ljava/lang/String;"))
                       : nullptr;
    const char* bytes = path != nullptr ? (*env)->GetStringUTFChars(env, path, nullptr) : nullptr;
    int written = bytes != nullptr
                      ? snprintf(documents->folder, sizeof(documents->folder), "%s" FOLDER, bytes)
                      : -1;
    found = found && written > 0 && (size_t)written < sizeof(documents->folder);
    if (bytes != nullptr)
    {
        (*env)->ReleaseStringUTFChars(env, path, bytes);
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    (*env)->DeleteLocalRef(env, activities);
    (*env)->DeleteLocalRef(env, cache);
    (*env)->DeleteLocalRef(env, files);
    (*env)->DeleteLocalRef(env, path);
    if (found)
    {
        // The copies of earlier runs.
        (void)nftw(documents->folder, RemoveEntry, 16, FTW_DEPTH | FTW_PHYS);
    }
    return found;
}

// Closes every descriptor of the copy and forgets it.
static void EndCopy(mwinAndroidPlatform* platform)
{
    mwinAndroidDocuments* documents = &platform->documents;
    const mwinAllocator* allocator = &platform->context->allocator;
    for (uint32_t i = documents->next; i < documents->count; i++)
    {
        if (documents->from != nullptr && documents->from[i] >= 0)
        {
            (void)close(documents->from[i]);
        }
        if (documents->into != nullptr && documents->into[i] >= 0)
        {
            (void)close(documents->into[i]);
        }
    }
    if (documents->from != nullptr)
    {
        mwinRelease(allocator, documents->from, documents->count * sizeof(int), alignof(int));
    }
    if (documents->into != nullptr)
    {
        mwinRelease(allocator, documents->into, documents->count * sizeof(int), alignof(int));
    }
    documents->from = nullptr;
    documents->into = nullptr;
    documents->count = 0;
    documents->next = 0;
    documents->copying = false;
}

void mwinAndroidStopDialogs(mwinAndroidPlatform* platform)
{
    EndCopy(platform);
    mwinAndroidDocuments* documents = &platform->documents;
    if (documents->type != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, documents->type);
        documents->type = nullptr;
    }
}

int mwinAndroidAskDialog(mwinAndroidPlatform* platform, uint32_t slot, uint32_t request)
{
    mwinContext* context = platform->context;
    mwinAndroidDocuments* documents = &platform->documents;
    const mwinDialogCopy* copy = context->windows[slot].requests[request].value.dialog;
    if (copy->kind == mwin_dialogSave || copy->kind == mwin_dialogFolder)
    {
        return mwin_outcomeUnsupported;
    }
    if (platform->activity == nullptr)
    {
        return mwin_outcomeFailed;
    }
    // The dialog before was superseded: its copy stops.
    EndCopy(platform);
    documents->to.waiting = false;
    uint32_t before = documents->serial & 0xFFu;
    documents->serial += 1;
    // Every filter's extensions, ';' between them.
    char extensions[MWIN_DIALOG_FILTERS * (MWIN_DIALOG_FILTER_BYTES + 1) + 1] = {0};
    size_t length = 0;
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        int added = snprintf(extensions + length, sizeof(extensions) - length, "%s%s",
                             length > 0 ? ";" : "", copy->filters[i].extensions);
        length += added > 0 ? (size_t)added : 0;
        length = length < sizeof(extensions) ? length : sizeof(extensions) - 1;
    }
    JNIEnv* env = platform->java.env;
    jstring text = (*env)->NewStringUTF(env, extensions);
    jboolean shown =
        text != nullptr && (*env)->CallStaticBooleanMethod(
                               env, documents->type, documents->open, platform->activity->clazz,
                               (jboolean)(copy->kind == mwin_dialogOpenMany), text,
                               (jint)(documents->serial & 0xFFu), (jint)before);
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, text);
    if (!shown || thrown)
    {
        return mwin_outcomeFailed;
    }
    documents->to = mwinAnswerTo(context, slot, request);
    return -1;
}

// Answers the dialog with how it went, settling its paths when done.
static void Finish(mwinAndroidPlatform* platform, mwinOutcome outcome)
{
    mwinAndroidDocuments* documents = &platform->documents;
    EndCopy(platform);
    if (mwinAnswerRequest(platform->context, &documents->to) != nullptr)
    {
        outcome =
            mwinSettleDialog(platform->context, documents->to.slot, documents->to.request, outcome);
    }
    mwinAnswer(platform->context, &documents->to, outcome);
}

// A copy's name: the provider's, with no '/', or "Document" for none
// or a name that is not one ("." and "..").
static void NameOf(const char* given, size_t length, char* name, size_t capacity)
{
    bool none = length == 0 || (length == 1 && given[0] == '.') ||
                (length == 2 && given[0] == '.' && given[1] == '.');
    if (none)
    {
        (void)snprintf(name, capacity, "Document");
        return;
    }
    size_t kept = length < capacity - 1 ? length : capacity - 1;
    for (size_t i = 0; i < kept; i++)
    {
        name[i] = given[i] == '/' ? '_' : given[i];
    }
    name[kept] = '\0';
}

// Makes a document's copy: its folder, the file opened to write, its
// path gathered. False when it could not be made.
static bool MakeCopy(mwinAndroidPlatform* platform, JNIEnv* env, jstring given, uint32_t index)
{
    mwinAndroidDocuments* documents = &platform->documents;
    size_t size = 0;
    char* bytes =
        given != nullptr ? mwinAndroidBytesOf(platform, env, given, &size, nullptr, 0) : nullptr;
    char name[256];
    NameOf(bytes, bytes != nullptr ? size : 0, name, sizeof(name));
    if (bytes != nullptr)
    {
        mwinRelease(&platform->context->allocator, bytes, size, 1);
    }
    // The dialogs' folder, this dialog's, and this document's.
    char folder[PATH_MAX];
    char path[PATH_MAX];
    (void)mkdir(documents->folder, 0700);
    (void)snprintf(folder, sizeof(folder), "%s/%u", documents->folder, documents->serial);
    (void)mkdir(folder, 0700);
    int length =
        snprintf(folder, sizeof(folder), "%s/%u/%u", documents->folder, documents->serial, index);
    if (length <= 0 || (size_t)length >= sizeof(folder) || mkdir(folder, 0700) != 0)
    {
        return false;
    }
    length = snprintf(path, sizeof(path), "%s/%s", folder, name);
    if (length <= 0 || (size_t)length >= sizeof(path))
    {
        return false;
    }
    documents->into[index] = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    mwinAddDialogFile(platform->context, path, (size_t)length);
    return documents->into[index] >= 0;
}

// Closes the descriptors Java opened that no copy takes.
static void CloseAll(JNIEnv* env, jintArray descriptors)
{
    jsize count = descriptors != nullptr ? (*env)->GetArrayLength(env, descriptors) : 0;
    jint* values = count > 0 ? (*env)->GetIntArrayElements(env, descriptors, nullptr) : nullptr;
    for (jsize i = 0; values != nullptr && i < count; i++)
    {
        if (values[i] >= 0)
        {
            (void)close(values[i]);
        }
    }
    if (values != nullptr)
    {
        (*env)->ReleaseIntArrayElements(env, descriptors, values, JNI_ABORT);
    }
}

// Takes the documents' descriptors and makes their copies: false when
// one could not be read or made.
static bool StartCopy(mwinAndroidPlatform* platform, JNIEnv* env, jintArray descriptors,
                      jobjectArray names, uint32_t count)
{
    mwinContext* context = platform->context;
    mwinAndroidDocuments* documents = &platform->documents;
    int* from = mwinAllocate(&context->allocator, count * sizeof(int), alignof(int));
    int* into = mwinAllocate(&context->allocator, count * sizeof(int), alignof(int));
    if (from == nullptr || into == nullptr)
    {
        // Nothing taken: Java's descriptors are closed here.
        if (from != nullptr)
        {
            mwinRelease(&context->allocator, from, count * sizeof(int), alignof(int));
        }
        if (into != nullptr)
        {
            mwinRelease(&context->allocator, into, count * sizeof(int), alignof(int));
        }
        CloseAll(env, descriptors);
        return false;
    }
    (*env)->GetIntArrayRegion(env, descriptors, 0, (jsize)count, from);
    for (uint32_t i = 0; i < count; i++)
    {
        into[i] = -1;
    }
    documents->from = from;
    documents->into = into;
    documents->count = count;
    documents->next = 0;
    documents->copying = true;
    mwinBeginDialog(context);
    bool made = true;
    for (uint32_t i = 0; made && i < count; i++)
    {
        jstring name = (*env)->GetObjectArrayElement(env, names, (jsize)i);
        // The copy never waits on a provider's pipe.
        made = from[i] >= 0 && MakeCopy(platform, env, name, i) &&
               fcntl(from[i], F_SETFL, fcntl(from[i], F_GETFL) | O_NONBLOCK) == 0;
        (*env)->DeleteLocalRef(env, name);
    }
    return made;
}

static void JNICALL Documents(JNIEnv* env, jclass type, jlong program, jint number, jboolean chosen,
                              jintArray descriptors, jobjectArray names)
{
    (void)type;
    mwinAndroidPlatform* platform = mwinAndroidProgramOf(program);
    // An answer to a dialog no longer waited for, or under an old number,
    // is dropped.
    bool waited = platform != nullptr && platform->documents.to.waiting &&
                  !platform->documents.copying &&
                  (uint32_t)number == (platform->documents.serial & 0xFFu);
    jsize count = descriptors != nullptr ? (*env)->GetArrayLength(env, descriptors) : 0;
    if (!waited || !chosen || count == 0 || (uint32_t)count > platform->context->limits.dialogFiles)
    {
        CloseAll(env, descriptors);
        if (waited)
        {
            Finish(platform, !chosen      ? mwin_outcomeCancelled
                             : count == 0 ? mwin_outcomeFailed
                                          : mwin_outcomeTooLarge);
        }
        return;
    }
    if (!StartCopy(platform, env, descriptors, names, (uint32_t)count))
    {
        Finish(platform, mwin_outcomeFailed);
    }
}

void mwinAndroidPumpDialogs(mwinAndroidPlatform* platform)
{
    mwinAndroidDocuments* documents = &platform->documents;
    if (!documents->copying)
    {
        return;
    }
    uint8_t block[BLOCK_BYTES];
    uint32_t moved = 0;
    while (documents->next < documents->count && moved < COPY_BYTES)
    {
        uint32_t i = documents->next;
        ssize_t got = read(documents->from[i], block, sizeof(block));
        if (got < 0 && (errno == EAGAIN || errno == EINTR))
        {
            return;
        }
        bool whole = got > 0 && write(documents->into[i], block, (size_t)got) == got;
        if (got != 0 && !whole)
        {
            Finish(platform, mwin_outcomeFailed);
            return;
        }
        if (got == 0)
        {
            // This document is whole.
            (void)close(documents->from[i]);
            bool closed = close(documents->into[i]) == 0;
            documents->next += 1;
            if (!closed)
            {
                Finish(platform, mwin_outcomeFailed);
                return;
            }
        }
        moved += (uint32_t)(got > 0 ? got : 0);
    }
    if (documents->next == documents->count)
    {
        Finish(platform, mwin_outcomeDone);
    }
}
