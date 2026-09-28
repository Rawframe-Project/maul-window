// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's accessibility root, with a provider of the test's
// own:
// - WM_GETOBJECT for an object of the program's is left to Windows and
//   tells nothing;
// - a UI Automation client asking tells the program once, and before a
//   root gets Windows' own answer;
// - with a root, a UI Automation client on another thread reads the
//   window's name from the provider;
// - once the window is gone, UI Automation holds the provider no more.

#include "test_harness.h"

#include "maul-window/accessibility.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>
#include <wchar.h>

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// COM's declarations, which lean Windows headers leave out, before UI
// Automation's.
#include <ole2.h>
#include <uiautomationclient.h>
#include <uiautomationcore.h>

#define DEADLINE_MS 10000u
// UiaRootObjectId of uiautomationcoreapi.h.
#define ROOT_OBJECT_ID (-25)
// An object id of the program's own, which Windows knows nothing of.
#define OWN_OBJECT_ID 1
#define NAME          L"maul root"

typedef struct Provider
{
    IRawElementProviderSimple iface;
    LONG refs;
    HWND hwnd;
} Provider;

static const GUID s_iidProvider = {
    0xd6dd68d1, 0x86fd, 0x4332, {0x86, 0x66, 0x9a, 0xbe, 0xde, 0xa2, 0xd2, 0x4c}};

static Provider* Of(IRawElementProviderSimple* iface)
{
    return (Provider*)iface;
}

static ULONG STDMETHODCALLTYPE AddRef(IRawElementProviderSimple* iface)
{
    return (ULONG)InterlockedIncrement(&Of(iface)->refs);
}

static ULONG STDMETHODCALLTYPE Release(IRawElementProviderSimple* iface)
{
    return (ULONG)InterlockedDecrement(&Of(iface)->refs);
}

static HRESULT STDMETHODCALLTYPE QueryInterface(IRawElementProviderSimple* iface, REFIID riid,
                                                void** out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &s_iidProvider))
    {
        *out = iface;
        AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE Options(IRawElementProviderSimple* iface,
                                         enum ProviderOptions* options)
{
    (void)iface;
    *options = ProviderOptions_ServerSideProvider;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Pattern(IRawElementProviderSimple* iface, PATTERNID pattern,
                                         IUnknown** out)
{
    (void)iface;
    (void)pattern;
    *out = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Property(IRawElementProviderSimple* iface, PROPERTYID property,
                                          VARIANT* out)
{
    (void)iface;
    VariantInit(out);
    if (property == UIA_NamePropertyId)
    {
        out->vt = VT_BSTR;
        out->bstrVal = SysAllocString(NAME);
    }
    return S_OK;
}

// The window's own provider, as a root is to give it; loaded as the
// backend loads UI Automation, which MinGW has no import library of.
static HRESULT STDMETHODCALLTYPE Host(IRawElementProviderSimple* iface,
                                      IRawElementProviderSimple** out)
{
    HRESULT(WINAPI * host)(HWND, IRawElementProviderSimple**) = nullptr;
    FARPROC found =
        GetProcAddress(LoadLibraryW(L"uiautomationcore.dll"), "UiaHostProviderFromHwnd");
    memcpy((void*)&host, (const void*)&found, sizeof(host));
    *out = nullptr;
    return host != nullptr ? host(Of(iface)->hwnd, out) : S_OK;
}

static IRawElementProviderSimpleVtbl s_vtbl = {QueryInterface, AddRef,   Release, Options,
                                               Pattern,        Property, Host};

typedef struct Program
{
    ULONGLONG startMs;
    mwinWindowId window;
    mwinRequestId request;
    int step;
    int asked;
    Provider provider;
    HANDLE client;
    // Set by the client thread once it read the name: 1 for the root's,
    // 2 for another.
    volatile LONG named;
    bool done;
} Program;

static int Outcome(mwinContext* context, Program* program)
{
    mwinEvent event;
    int outcome = -1;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->asked += event.type == mwin_eventAccessibilityRequested;
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == program->request.index1 &&
            completion->request.generation == program->request.generation)
        {
            outcome = completion->outcome;
        }
    }
    return outcome;
}

static HWND Handle(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (HWND)handles.handles.win32.hwnd
               : nullptr;
}

// A UI Automation client, which may not run on the window's thread.
static DWORD WINAPI Client(void* user)
{
    Program* program = user;
    LONG named = 2;
    IUIAutomation* automation = nullptr;
    IUIAutomationElement* element = nullptr;
    BSTR name = nullptr;
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        if (SUCCEEDED(CoCreateInstance(&CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                       &IID_IUIAutomation, (void**)&automation)) &&
            SUCCEEDED(
                IUIAutomation_ElementFromHandle(automation, program->provider.hwnd, &element)) &&
            SUCCEEDED(IUIAutomationElement_get_CurrentName(element, &name)) && name != nullptr &&
            wcscmp(name, NAME) == 0)
        {
            named = 1;
        }
        SysFreeString(name);
        if (element != nullptr)
        {
            IUIAutomationElement_Release(element);
        }
        if (automation != nullptr)
        {
            IUIAutomation_Release(automation);
        }
        CoUninitialize();
    }
    InterlockedExchange(&program->named, named);
    return 0;
}

static void Next(mwinContext* context, Program* program, int outcome)
{
    HWND hwnd = Handle(context, program->window);
    if (program->step == 0)
    {
        int before = program->asked;
        (void)SendMessageW(hwnd, WM_GETOBJECT, 0, OWN_OBJECT_ID);
        (void)Outcome(context, program);
        bool quiet = program->asked == before;
        LRESULT none = SendMessageW(hwnd, WM_GETOBJECT, 0, ROOT_OBJECT_ID);
        (void)SendMessageW(hwnd, WM_GETOBJECT, 0, ROOT_OBJECT_ID);
        (void)Outcome(context, program);
        CHECK(quiet, "an object of the program's left to Windows, telling nothing");
        CHECK(program->asked == 1 && none == 0,
              "a UI Automation client asking tells once, and gets no root before one");
        program->provider.hwnd = hwnd;
        CHECK(mwinRequestAccessibilityRoot(context, program->window, &program->provider.iface,
                                           &program->request) == mwin_success,
              "a root");
    }
    else if (program->step == 1)
    {
        CHECK(outcome == mwin_outcomeDone, "the root taken");
        program->client = CreateThread(nullptr, 0, Client, program, 0, nullptr);
        CHECK(program->client != nullptr, "a client");
    }
    else
    {
        CHECK(program->named == 1, "a UI Automation client reads the root's name");
        WaitForSingleObject(program->client, INFINITE);
        CloseHandle(program->client);
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success &&
                  program->provider.refs == 1,
              "the window gone, UI Automation holds the root no more");
        program->done = true;
    }
    program->step += 1;
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    program->provider = (Provider){.iface = {&s_vtbl}, .refs = 1};
    mwinWindowDef def = mwinDefaultWindowDef();
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    int outcome = program->done ? -1 : Outcome(context, program);
    if (outcome >= 0 || (program->step == 2 && program->named != 0))
    {
        Next(context, program, outcome);
    }
    bool late = GetTickCount64() - program->startMs > DEADLINE_MS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs");
    // A client still waiting on a window that is gone ends with the
    // process.
    if (program.client != nullptr && !program.done)
    {
        (void)WaitForSingleObject(program.client, 1000);
    }
    return s_failures == 0 ? 0 : 1;
}
