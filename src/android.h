// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the Android backend keeps (mwin-0026): the activity the program
// shows in, and the Java side reached through it; the activity's native
// window and input queue; what drives the frames; whether the activity
// is started and whether the program was told it stopped; and the one
// window, with what the program was last told of it. Everything runs on
// the main thread.

#ifndef MAUL_WINDOW_SRC_ANDROID_H
#define MAUL_WINDOW_SRC_ANDROID_H

#include "core.h"

#include <android/choreographer.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <jni.h>

typedef struct mwinAndroidPlatform mwinAndroidPlatform;

// The window: whether its first surface came (mwin_eventWindowCreated
// posted) and whether its surface is gone; its size in pixels, scale and
// focus as last posted.
typedef struct mwinAndroidWindow
{
    bool created;
    bool lost;
    uint32_t width;
    uint32_t height;
    float scale;
    bool focused;
} mwinAndroidWindow;

// The Java side, reached through the first activity: the main thread's
// JNI environment, the activity's class (a global reference) and its
// field that holds the running program (maul.window.Activity.program).
typedef struct mwinAndroidJava
{
    JNIEnv* env;
    jclass activityClass;
    jfieldID program;
} mwinAndroidJava;

struct mwinAndroidPlatform
{
    mwinContext* context;
    mwinAndroidJava java;
    // The activity the program shows in, null between activities; its
    // native window (acquired) and input queue, null when it has none;
    // its configuration.
    ANativeActivity* activity;
    ANativeWindow* nativeWindow;
    AInputQueue* queue;
    AConfiguration* configuration;
    // The main thread's looper and choreographer; whether a frame
    // callback waits, which nothing can take back: a program that ends
    // meanwhile leaves the context for it to free.
    ALooper* looper;
    AChoreographer* choreographer;
    bool framePosted;
    bool ended;
    // Between the activity's onStart and onStop; whether the program was
    // told the application stopped running; whether the activity's window
    // has the focus.
    bool started;
    bool suspended;
    bool focused;
    // The window's slot, -1 when there is none.
    int32_t slot;
    mwinAndroidWindow window;
};

// The platform of a context whose backend is Android.
mwinAndroidPlatform* mwinAndroidPlatformOf(const mwinContext* context);

// Nanoseconds on the clock input events count (CLOCK_MONOTONIC).
uint64_t mwinAndroidNow(void);

// The scale the activity's configuration gives: its density over 160.
float mwinAndroidScale(const mwinAndroidPlatform* platform);

// The activity's native window came or went: the window, if any, is
// created at its first surface, its surface lost and restored after
// (android_window.c). Its size or scale may have changed; the activity's
// focus may have.
void mwinAndroidSurfaceCame(mwinAndroidPlatform* platform);
void mwinAndroidSurfaceWent(mwinAndroidPlatform* platform);
void mwinAndroidReadSize(mwinAndroidPlatform* platform);
void mwinAndroidPostFocus(mwinAndroidPlatform* platform);

// The backend's window operations (android_window.c).
void mwinAndroidCreateWindow(mwinContext* context, uint32_t slot);
void mwinAndroidDestroyWindow(mwinContext* context, uint32_t slot);
void mwinAndroidSubmit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_ANDROID_H
