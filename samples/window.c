// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// One window, and what the platform says about it: its size, scale and
// pixels as they change, focus, keys and the text they type, and an
// input method's compositions. F11 switches to fullscreen and back, T
// starts and stops accepting text, Escape or the close button ends the
// program cleanly.
//
// The window draws nothing: drawing is Maul RHI's, from the window's
// native handles. On Wayland a surface shows only once something is
// drawn to it, so there the window stays unseen while the notifications
// still come.

// nanosleep, outside ISO C.
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "maul-window/window.h"

#include "maul-window/context.h"
#include "maul-window/event.h"

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <time.h>
#endif

typedef struct Sample
{
    mwinWindowId window;
    bool fullscreen;
    bool texting;
} Sample;

// The desktop's loop does not wait for the display: a frame's worth of
// sleep keeps it from spinning. The browser paces the web's frames.
static void Rest(void)
{
#if defined(_WIN32)
    Sleep(16);
#elif !defined(__EMSCRIPTEN__)
    struct timespec pause = {0, 16000000};
    (void)nanosleep(&pause, nullptr);
#endif
}

static void OnKey(Sample* sample, mwinContext* context, const mwinKeyEvent* key)
{
    if (key->code == mwin_codeF11)
    {
        sample->fullscreen = !sample->fullscreen;
        mwinWindowMode mode =
            sample->fullscreen ? mwin_modeBorderlessFullscreen : mwin_modeWindowed;
        (void)mwinRequestMode(context, sample->window, mode, nullptr);
    }
    else if (key->code == mwin_codeKeyT && !sample->texting)
    {
        sample->texting = true;
        // The caret near the top left, for the input method's windows.
        mwinRect caret = {20.0f, 20.0f, 2.0f, 20.0f};
        (void)mwinRequestTextInput(context, sample->window, true, caret, nullptr);
        printf("accepting text; Escape stops\n");
    }
}

// Prints a notification; false when the program should end.
static bool Show(Sample* sample, mwinContext* context, const mwinEvent* event)
{
    switch (event->type)
    {
    case mwin_eventResized:
        printf("size %.0f x %.0f\n", (double)event->data.size.width,
               (double)event->data.size.height);
        break;
    case mwin_eventPixelSizeChanged:
        printf("pixels %u x %u\n", event->data.pixelSize.width, event->data.pixelSize.height);
        break;
    case mwin_eventScaleChanged:
        printf("scale %.2f\n", (double)event->data.scale.scale);
        break;
    case mwin_eventFocusGained:
    case mwin_eventFocusLost:
        printf("focus %s\n", event->type == mwin_eventFocusGained ? "gained" : "lost");
        break;
    case mwin_eventTextInput:
        printf("text \"%.*s\"\n", (int)event->data.text.length, event->data.text.text);
        break;
    case mwin_eventImePreedit:
        printf("composing \"%.*s\"\n", (int)event->data.preedit.length, event->data.preedit.text);
        break;
    case mwin_eventKeyDown:
        if (event->data.key.code == mwin_codeEscape && sample->texting)
        {
            sample->texting = false;
            (void)mwinRequestTextInput(context, sample->window, false, (mwinRect){0}, nullptr);
            printf("not accepting text\n");
            break;
        }
        if (event->data.key.code == mwin_codeEscape)
        {
            return false;
        }
        OnKey(sample, context, &event->data.key);
        break;
    case mwin_eventCloseRequested:
        return false;
    default:
        break;
    }
    return true;
}

static mwinResult Init(mwinContext* context, void* user)
{
    Sample* sample = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Maul Window";
    def.titleLength = 11;
    def.size = (mwinSize){800.0f, 600.0f};
    printf("F11 fullscreen, T text, Escape to end\n");
    return mwinCreateWindow(context, &def, &sample->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Sample* sample = user;
    mwinEvent event;
    bool running = true;
    while (running && mwinNextEvent(context, &event) == mwin_success)
    {
        running = Show(sample, context, &event);
    }
    Rest();
    return running ? mwin_frameContinue : mwin_frameStop;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)user;
    // Windows still open close with the context.
    (void)context;
    printf("ended: %s\n", status == mwin_success ? "cleanly" : "init failed");
}

int main(void)
{
    static Sample sample;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &sample;
    mwinResult status = mwinRun(&def);
    if (status != mwin_success)
    {
        printf("could not run: %d\n", (int)status);
    }
    return status == mwin_success ? 0 : 1;
}
