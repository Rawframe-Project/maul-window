// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32's touch keyboard and input purpose.

#include "win32_touch_keyboard.h"

#include <string.h>

// The input scopes of inputscope.h the purposes map to.
enum
{
    scopeDefault = 0,
    scopeUrl = 1,
    scopeEmail = 5,
    scopeNumber = 29,
    scopePassword = 31,
};

// The first six methods of every runtime interface: IUnknown's, then
// IInspectable's, which nothing here calls.
#define INSPECTABLE                                                                                \
    void* QueryInterface;                                                                          \
    void* AddRef;                                                                                  \
    ULONG(STDMETHODCALLTYPE* Release)(void* self);                                                 \
    void* GetIids;                                                                                 \
    void* GetRuntimeClassName;                                                                     \
    void* GetTrustLevel;

typedef struct InputPane
{
    const struct
    {
        INSPECTABLE
        HRESULT(STDMETHODCALLTYPE* tryShow)(struct InputPane* self, boolean* accepted);
        HRESULT(STDMETHODCALLTYPE* tryHide)(struct InputPane* self, boolean* accepted);
    }* v;
} InputPane;

typedef struct InputPaneInterop
{
    const struct
    {
        INSPECTABLE
        HRESULT(STDMETHODCALLTYPE* getForWindow)(struct InputPaneInterop* self, HWND window,
                                                 const IID* iid, void** pane);
    }* v;
} InputPaneInterop;

// IInputPaneInterop and IInputPane2, as inputpaneinterop.h and
// Windows.UI.ViewManagement.h define them.
static const IID s_iidInterop = {
    0x75CF2C57, 0x9195, 0x4931, {0x83, 0x32, 0xF0, 0xB4, 0x09, 0xE9, 0x16, 0xAF}};
static const IID s_iidPane2 = {
    0x8A6B3F26, 0x7090, 0x4793, {0x94, 0x4C, 0xC3, 0xF2, 0xCD, 0xE2, 0x62, 0x76}};

// A library's function, its bytes copied: a FARPROC is no function of
// the right type to cast.
static bool Load(HMODULE library, const char* name, void* function, size_t size)
{
    FARPROC found = library != nullptr ? GetProcAddress(library, name) : nullptr;
    memcpy(function, (const void*)&found, size);
    return found != nullptr;
}

// Loads what the requests use, once; each part may be missing.
static void LoadOnce(mwinWin32Platform* platform)
{
    if (platform->keyboardTried)
    {
        return;
    }
    platform->keyboardTried = true;
    platform->msctf = LoadLibraryW(L"msctf.dll");
    (void)Load(platform->msctf, "SetInputScope", (void*)&platform->setInputScope,
               sizeof(platform->setInputScope));
    platform->combase = LoadLibraryW(L"combase.dll");
    if (!Load(platform->combase, "RoGetActivationFactory", (void*)&platform->activate,
              sizeof(platform->activate)) ||
        !Load(platform->combase, "WindowsCreateString", (void*)&platform->makeString,
              sizeof(platform->makeString)) ||
        !Load(platform->combase, "WindowsDeleteString", (void*)&platform->deleteString,
              sizeof(platform->deleteString)))
    {
        platform->activate = nullptr;
    }
}

static int ScopeOf(mwinInputPurpose purpose)
{
    switch (purpose)
    {
    case mwin_purposeNumber:
        return scopeNumber;
    case mwin_purposeEmail:
        return scopeEmail;
    case mwin_purposePassword:
        return scopePassword;
    case mwin_purposeUrl:
        return scopeUrl;
    default:
        return scopeDefault;
    }
}

// The window's InputPane, or NULL where Windows has none for desktop
// windows (before Windows 10 1607).
static InputPane* PaneOf(const mwinWin32Platform* platform, HWND hwnd)
{
    static const WCHAR name[] = L"Windows.UI.ViewManagement.InputPane";
    void* string = nullptr;
    InputPaneInterop* interop = nullptr;
    InputPane* pane = nullptr;
    if (platform->activate == nullptr ||
        FAILED(platform->makeString(name, (UINT32)(sizeof(name) / sizeof(name[0]) - 1), &string)))
    {
        return nullptr;
    }
    if (SUCCEEDED(platform->activate(string, &s_iidInterop, (void**)&interop)))
    {
        if (FAILED(interop->v->getForWindow(interop, hwnd, &s_iidPane2, (void**)&pane)))
        {
            pane = nullptr;
        }
        (void)interop->v->Release(interop);
    }
    (void)platform->deleteString(string);
    return pane;
}

mwinOutcome mwinWin32SetTouchKeyboard(mwinWin32Window* window, bool visible,
                                      mwinInputPurpose purpose)
{
    mwinWin32Platform* platform = window->platform;
    LoadOnce(platform);
    if (platform->setInputScope != nullptr)
    {
        (void)platform->setInputScope(window->hwnd, ScopeOf(purpose));
    }
    InputPane* pane = PaneOf(platform, window->hwnd);
    if (pane == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    boolean accepted = 0;
    HRESULT result =
        visible ? pane->v->tryShow(pane, &accepted) : pane->v->tryHide(pane, &accepted);
    (void)pane->v->Release(pane);
    return SUCCEEDED(result) && accepted ? mwin_outcomeDone : mwin_outcomeDenied;
}

void mwinWin32StopTouchKeyboard(mwinWin32Platform* platform)
{
    if (platform->msctf != nullptr)
    {
        FreeLibrary(platform->msctf);
    }
    if (platform->combase != nullptr)
    {
        FreeLibrary(platform->combase);
    }
    platform->msctf = nullptr;
    platform->combase = nullptr;
    platform->setInputScope = nullptr;
    platform->activate = nullptr;
    platform->keyboardTried = false;
}
