// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's accessibility root, with a provider of the test's
// own:
// - WM_GETOBJECT for other objects is left to Windows and tells nothing;
// - a UI Automation client asking tells the program once, and before a
//   root gets Windows' own answer;
// - with a root, the client gets it through UI Automation: an answer it
//   can take the object from, holding the provider until it lets go.
// UI Automation keeps no provider that raised no event, so letting go
// of a destroyed window's is not seen here.

#include "test_harness.h"

#include "maul-window/accessibility.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <oleacc.h>
#include <uiautomationcore.h>
#include <windows.h>

#define DEADLINE_MS 10000u
// UiaRootObjectId of uiautomationcoreapi.h.
#define ROOT_OBJECT_ID (-25)

typedef struct Provider
{
    IRawElementProviderSimple iface;
    LONG refs;
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
    (void)property;
    VariantInit(out);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Host(IRawElementProviderSimple* iface,
                                      IRawElementProviderSimple** out)
{
    (void)iface;
    *out = nullptr;
    return S_OK;
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

static void Next(mwinContext* context, Program* program, int outcome)
{
    HWND hwnd = Handle(context, program->window);
    if (program->step == 0)
    {
        LRESULT client = SendMessageW(hwnd, WM_GETOBJECT, 0, (LPARAM)OBJID_CLIENT);
        (void)Outcome(context, program);
        bool quiet = program->asked == 0;
        LRESULT none = SendMessageW(hwnd, WM_GETOBJECT, 0, ROOT_OBJECT_ID);
        (void)SendMessageW(hwnd, WM_GETOBJECT, 0, ROOT_OBJECT_ID);
        (void)Outcome(context, program);
        (void)client;
        CHECK(quiet, "other objects left to Windows, telling nothing");
        CHECK(program->asked == 1 && none == 0,
              "a UI Automation client asking tells once, and gets no root before one");
        CHECK(mwinRequestAccessibilityRoot(context, program->window, &program->provider.iface,
                                           &program->request) == mwin_success,
              "a root");
    }
    else
    {
        CHECK(outcome == mwin_outcomeDone, "the root taken");
        LRESULT given = SendMessageW(hwnd, WM_GETOBJECT, 0, ROOT_OBJECT_ID);
        LONG held = program->provider.refs;
        IUnknown* taken = nullptr;
        bool took = given != 0 && held > 1 &&
                    SUCCEEDED(ObjectFromLresult(given, &IID_IUnknown, 0, (void**)&taken));
        if (taken != nullptr)
        {
            IUnknown_Release(taken);
        }
        CHECK(took && program->provider.refs == 1,
              "the client takes the root from the answer, held until it lets go");
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "destroy");
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
    if (outcome >= 0)
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
    return s_failures == 0 ? 0 : 1;
}
