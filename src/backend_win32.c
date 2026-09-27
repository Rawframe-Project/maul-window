// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend: the window class, DPI awareness and the pump. The
// pump never waits: it dispatches every message the thread has, which
// calls the window procedure. Win32 ties a window to the thread that
// made it, so the program runs on that thread (Main thread only).

#include "allocator.h"
#include "backend.h"
#include "core.h"
#include "win32.h"
#include "win32_output.h"
#include "win32_window.h"

#include <string.h>

static mwinWin32Platform* PlatformOf(const mwinContext* context)
{
    return (mwinWin32Platform*)context->backendData;
}

static size_t PlatformBytes(const mwinContext* context)
{
    const mwinLimits* limits = &context->limits;
    return sizeof(mwinWin32Platform) + limits->windows * sizeof(mwinWin32Window) +
           limits->monitors * sizeof(mwinWin32Output) + (limits->titleBytes + 1) * sizeof(WCHAR);
}

// Makes the process per-monitor DPI aware, unless it chose an awareness
// itself, in its manifest or by a call.
static void BecomeDpiAware(void)
{
    DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
    if (awareness == DPI_AWARENESS_UNAWARE)
    {
        (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

// Registers the window class, or finds it registered by an earlier
// context of the process.
static bool RegisterWindowClass(mwinWin32Platform* platform)
{
    WNDCLASSEXW windowClass = {0};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = mwinWin32WindowProc;
    windowClass.hInstance = platform->instance;
    windowClass.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    windowClass.lpszClassName = MWIN_WIN32_CLASS;
    platform->windowClass = RegisterClassExW(&windowClass);
    return platform->windowClass != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void Stop(mwinContext* context)
{
    mwinWin32Platform* platform = PlatformOf(context);
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        if (platform->windows[i].hwnd != nullptr)
        {
            mwinWin32DestroyWindow(context, i);
        }
    }
    if (platform->windowClass != 0)
    {
        UnregisterClassW(MWIN_WIN32_CLASS, platform->instance);
    }
    mwinRelease(&context->allocator, platform, PlatformBytes(context), alignof(max_align_t));
    context->backendData = nullptr;
}

static mwinResult Start(mwinContext* context)
{
    unsigned char* block =
        mwinAllocate(&context->allocator, PlatformBytes(context), alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, PlatformBytes(context));
    const mwinLimits* limits = &context->limits;
    mwinWin32Platform* platform = (mwinWin32Platform*)block;
    unsigned char* storage = block + sizeof(mwinWin32Platform);
    platform->windows = (mwinWin32Window*)storage;
    storage += limits->windows * sizeof(mwinWin32Window);
    platform->outputs = (mwinWin32Output*)storage;
    storage += limits->monitors * sizeof(mwinWin32Output);
    platform->title = (WCHAR*)storage;
    for (uint32_t i = 0; i < limits->monitors; i++)
    {
        platform->outputs[i].monitor = -1;
    }
    platform->context = context;
    platform->instance = GetModuleHandleW(nullptr);
    context->backendData = platform;
    BecomeDpiAware();
    if (!RegisterWindowClass(platform))
    {
        Stop(context);
        return mwin_errorPlatform;
    }
    mwinWin32RefreshMonitors(platform);
    return mwin_success;
}

static void Pump(mwinContext* context)
{
    (void)context;
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static mwinResult Run(mwinContext* context)
{
    return mwinRunLoop(context, Pump);
}

static uint64_t Now(const mwinContext* context)
{
    (void)context;
    return mwinWin32Now();
}

// Without a keyboard yet, every key is its code's name.
static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    (void)context;
    return MWIN_KEY_NAMED | code;
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    (void)context;
    (void)buffer;
    (void)capacity;
    *lengthOut = 0;
    return mwin_success;
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    const mwinWin32Platform* platform = PlatformOf(context);
    out->platform = mwin_platformWin32;
    out->handles.win32.hwnd = platform->windows[slot].hwnd;
    out->handles.win32.hinstance = platform->instance;
}

const mwinBackendOps mwinWin32Backend = {
    Start,           Stop, Run,        mwinWin32CreateWindow, mwinWin32DestroyWindow,
    mwinWin32Submit, Now,  MapKeyCode, KeyboardLayout,        NativeHandles,
};
