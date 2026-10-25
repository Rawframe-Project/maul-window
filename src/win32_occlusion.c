// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Occlusion on Win32.

#include "win32_occlusion.h"

#include <dwmapi.h>
#include <string.h>
#include <wtsapi32.h>

// GUID_SESSION_DISPLAY_STATUS, defined here so that no GUID library is
// linked: its data is 0 for off, 1 for on and 2 for dimmed.
static const GUID s_displayStatus = {
    0x2B84C20E, 0xAD23, 0x4DDF, {0x93, 0xDB, 0x05, 0xFF, 0xBD, 0x7E, 0xFC, 0xA5}};

static void Update(mwinWin32Window* window)
{
    const mwinWin32Platform* platform = window->platform;
    bool occluded =
        window->minimized || window->cloaked || platform->locked || platform->displayOff;
    if (occluded == window->occluded)
    {
        return;
    }
    window->occluded = occluded;
    mwinEvent event = {.type = occluded ? mwin_eventOccluded : mwin_eventRevealed,
                       .timeNs = mwinWin32Now()};
    mwinPost(platform->context, window->slot, &event);
}

static void UpdateAll(mwinWin32Platform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        if (platform->windows[i].hwnd != nullptr)
        {
            Update(&platform->windows[i]);
        }
    }
}

static bool Cloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
           cloaked != 0;
}

// The library's window a handle names: the hook hears of every window of
// the thread, other code's too, so the class is checked before the
// window's pointer is read.
static mwinWin32Window* WindowOf(HWND hwnd)
{
    WCHAR name[sizeof(MWIN_WIN32_CLASS) / sizeof(WCHAR) + 1];
    int length = GetClassNameW(hwnd, name, (int)(sizeof(name) / sizeof(name[0])));
    if (length <= 0 || wcscmp(name, MWIN_WIN32_CLASS) != 0)
    {
        return nullptr;
    }
    mwinWin32Window* window = mwinWin32Pointer(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return window != nullptr && window->hwnd == hwnd ? window : nullptr;
}

static void CALLBACK OnCloak(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child,
                             DWORD thread, DWORD time)
{
    (void)hook;
    (void)event;
    (void)child;
    (void)thread;
    (void)time;
    mwinWin32Window* window = object == OBJID_WINDOW && hwnd != nullptr ? WindowOf(hwnd) : nullptr;
    if (window != nullptr)
    {
        window->cloaked = Cloaked(hwnd);
        Update(window);
    }
}

void mwinWin32StartOcclusion(mwinWin32Platform* platform)
{
    // Out of context, the events come through the thread's own message
    // loop: no thread of the library's.
    platform->cloakHook =
        SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, OnCloak,
                        GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
}

void mwinWin32StopOcclusion(mwinWin32Platform* platform)
{
    if (platform->cloakHook != nullptr)
    {
        UnhookWinEvent(platform->cloakHook);
        platform->cloakHook = nullptr;
    }
}

void mwinWin32WatchOcclusion(mwinWin32Window* window)
{
    window->minimized = IsIconic(window->hwnd) != FALSE;
    window->cloaked = Cloaked(window->hwnd);
    window->sessionWatched =
        WTSRegisterSessionNotification(window->hwnd, NOTIFY_FOR_THIS_SESSION) != FALSE;
    // Windows tells the display's state at once, then each change.
    window->displayWatch = RegisterPowerSettingNotification(window->hwnd, &s_displayStatus,
                                                            DEVICE_NOTIFY_WINDOW_HANDLE);
    Update(window);
}

void mwinWin32UnwatchOcclusion(mwinWin32Window* window)
{
    if (window->sessionWatched)
    {
        WTSUnRegisterSessionNotification(window->hwnd);
    }
    if (window->displayWatch != nullptr)
    {
        UnregisterPowerSettingNotification(window->displayWatch);
    }
}

// The display's state a power setting message tells: 0 for off, 1 for
// on, 2 for dimmed; -1 for a message about another setting.
static int DisplayStatus(WPARAM wParam, LPARAM lParam)
{
    const POWERBROADCAST_SETTING* setting = mwinWin32Pointer(lParam);
    if (wParam != PBT_POWERSETTINGCHANGE || setting == nullptr ||
        memcmp(&setting->PowerSetting, &s_displayStatus, sizeof(GUID)) != 0 ||
        setting->DataLength < sizeof(DWORD))
    {
        return -1;
    }
    DWORD status = 0;
    memcpy(&status, setting->Data, sizeof(status));
    return status <= 2 ? (int)status : 1;
}

void mwinWin32ObserveOcclusion(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM lParam)
{
    mwinWin32Platform* platform = window->platform;
    switch (message)
    {
    case WM_SIZE:
        window->minimized = wParam == SIZE_MINIMIZED;
        Update(window);
        break;
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_UNLOCK)
        {
            platform->locked = wParam == WTS_SESSION_LOCK;
            UpdateAll(platform);
        }
        break;
    case WM_POWERBROADCAST:
    {
        // A dimmed display still shows the window.
        int status = DisplayStatus(wParam, lParam);
        if (status >= 0)
        {
            platform->displayOff = status == 0;
            UpdateAll(platform);
        }
        break;
    }
    default:
        break;
    }
}
