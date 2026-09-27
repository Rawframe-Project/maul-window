// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's drag and drop, its drop target driven as OLE
// would drive it, with a data object of the test's: a drag of files and
// text reported where it enters and moves, a drag over that has not
// moved not reported again, and a drop delivering the paths, a path
// with a lone surrogate left out, and the text repaired; a drag of
// neither refused and not reported; a drag that leaves. Then, with the
// thread made multithreaded first so OLE cannot start, files dropped
// through WM_DROPFILES.

// The interfaces' function tables const, as the test's are.
#define CONST_VTABLE

#include "test_harness.h"
#include "win32.h"

#include "maul-window/drop.h"
#include "maul-window/event.h"

#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string.h>

#define DEADLINE_MS 10000u
#define MAX_RECORDS 32

// A data object that holds files, text, both or neither.
typedef struct Data
{
    IDataObject object;
    bool files;
    bool text;
} Data;

typedef struct Program
{
    ULONGLONG startMs;
    mwinWindowId window;
    // OLE cannot start: files come through WM_DROPFILES.
    bool fallback;
    bool done;
    mwinEvent records[MAX_RECORDS];
    int count;
} Program;

// Three paths, the last with a lone surrogate; each ended, and the list
// ended again.
static const WCHAR s_paths[] = L"C:\\a.txt\0C:\\\x00E9.png\0C:\\bad\xD800\0";

static HGLOBAL Global(const void* bytes, size_t size, size_t offset)
{
    HGLOBAL memory = GlobalAlloc(GHND, offset + size);
    char* at = GlobalLock(memory);
    memcpy(at + offset, bytes, size);
    GlobalUnlock(memory);
    return memory;
}

// The paths as a file drop, dropped at a point of the client area.
static HGLOBAL Files(POINT at)
{
    HGLOBAL memory = Global(s_paths, sizeof(s_paths), sizeof(DROPFILES));
    DROPFILES* header = GlobalLock(memory);
    *header = (DROPFILES){.pFiles = sizeof(DROPFILES), .pt = at, .fNC = FALSE, .fWide = TRUE};
    GlobalUnlock(memory);
    return memory;
}

static Data* DataOf(IDataObject* object)
{
    return (Data*)(void*)object;
}

static bool Holds(const Data* data, const FORMATETC* format)
{
    return (format->tymed & TYMED_HGLOBAL) != 0 &&
           ((format->cfFormat == CF_HDROP && data->files) ||
            (format->cfFormat == CF_UNICODETEXT && data->text));
}

static HRESULT STDMETHODCALLTYPE DataQuery(IDataObject* object, REFIID id, void** out)
{
    (void)object;
    (void)id;
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DataReference(IDataObject* object)
{
    (void)object;
    return 1;
}

static HRESULT STDMETHODCALLTYPE DataGet(IDataObject* object, FORMATETC* format, STGMEDIUM* medium)
{
    static const WCHAR text[] = {L'h', L'i', 0xD800, L'!', 0};
    if (!Holds(DataOf(object), format))
    {
        return DV_E_FORMATETC;
    }
    *medium = (STGMEDIUM){.tymed = TYMED_HGLOBAL};
    medium->hGlobal =
        format->cfFormat == CF_HDROP ? Files((POINT){0, 0}) : Global(text, sizeof(text), 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DataGetHere(IDataObject* object, FORMATETC* format,
                                             STGMEDIUM* medium)
{
    (void)object;
    (void)format;
    (void)medium;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE DataQueryGet(IDataObject* object, FORMATETC* format)
{
    return Holds(DataOf(object), format) ? S_OK : DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE DataCanonical(IDataObject* object, FORMATETC* in, FORMATETC* out)
{
    (void)object;
    (void)in;
    (void)out;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE DataSet(IDataObject* object, FORMATETC* format, STGMEDIUM* medium,
                                         BOOL release)
{
    (void)object;
    (void)format;
    (void)medium;
    (void)release;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE DataEnum(IDataObject* object, DWORD direction,
                                          IEnumFORMATETC** out)
{
    (void)object;
    (void)direction;
    *out = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE DataAdvise(IDataObject* object, FORMATETC* format, DWORD flags,
                                            IAdviseSink* sink, DWORD* connection)
{
    (void)object;
    (void)format;
    (void)flags;
    (void)sink;
    (void)connection;
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DataUnadvise(IDataObject* object, DWORD connection)
{
    (void)object;
    (void)connection;
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DataEnumAdvise(IDataObject* object, IEnumSTATDATA** out)
{
    (void)object;
    *out = nullptr;
    return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl s_data = {
    DataQuery,     DataReference, DataReference, DataGet,    DataGetHere,  DataQueryGet,
    DataCanonical, DataSet,       DataEnum,      DataAdvise, DataUnadvise, DataEnumAdvise,
};

static void Collect(Program* program, mwinContext* context)
{
    program->count = 0;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
}

// A point of the client area on the screen.
static POINTL Screen(HWND hwnd, LONG x, LONG y)
{
    POINT point = {x, y};
    ClientToScreen(hwnd, &point);
    return (POINTL){point.x, point.y};
}

static bool At(const mwinEvent* event, mwinEventType type, float scale, LONG x, LONG y)
{
    mwinPosition position =
        type == mwin_eventDropped ? event->data.drop.position : event->data.drag.position;
    return event->type == type && position.x == (float)x / scale && position.y == (float)y / scale;
}

static bool Paths(mwinContext* context, uint32_t drop)
{
    char paths[64];
    size_t length = 0;
    static const char expected[] = "C:\\a.txt\0C:\\\xC3\xA9.png";
    return mwinGetDroppedFiles(context, drop, paths, sizeof(paths), &length) == mwin_success &&
           length == sizeof(expected) && memcmp(paths, expected, length) == 0;
}

static void Drag(Program* program, mwinContext* context, IDropTarget* target, HWND hwnd,
                 float scale)
{
    Data both = {{&s_data}, true, true};
    Data neither = {{&s_data}, false, false};
    Data files = {{&s_data}, true, false};
    DWORD effect = DROPEFFECT_COPY;
    target->lpVtbl->DragEnter(target, &both.object, 0, Screen(hwnd, 10, 20), &effect);
    CHECK(effect == DROPEFFECT_COPY, "a drag of files and text taken");
    target->lpVtbl->DragOver(target, 0, Screen(hwnd, 10, 20), &effect);
    target->lpVtbl->DragOver(target, 0, Screen(hwnd, 30, 40), &effect);
    target->lpVtbl->Drop(target, &both.object, 0, Screen(hwnd, 50, 60), &effect);
    Collect(program, context);
    const mwinEvent* records = program->records;
    CHECK(program->count == 3 && At(&records[0], mwin_eventDragEntered, scale, 10, 20) &&
              records[0].data.drag.contents == (mwin_dragFiles | mwin_dragText) &&
              At(&records[1], mwin_eventDragMoved, scale, 30, 40) &&
              At(&records[2], mwin_eventDropped, scale, 50, 60),
          "entered, moved once, dropped");
    const mwinDropEvent* drop = &records[2].data.drop;
    char text[16];
    size_t length = 0;
    CHECK(drop->fileCount == 2 && drop->truncated && Paths(context, drop->drop) &&
              mwinGetDroppedText(context, drop->drop, text, sizeof(text), &length) ==
                  mwin_success &&
              length == 6 && memcmp(text, "hi\xEF\xBF\xBD!", 6) == 0,
          "the paths, one with a lone surrogate left out, and the text repaired");
    effect = DROPEFFECT_COPY;
    target->lpVtbl->DragEnter(target, &neither.object, 0, Screen(hwnd, 5, 5), &effect);
    target->lpVtbl->DragLeave(target);
    CHECK(effect == DROPEFFECT_NONE, "a drag of neither refused");
    target->lpVtbl->DragEnter(target, &files.object, 0, Screen(hwnd, 5, 5), &effect);
    target->lpVtbl->DragLeave(target);
    Collect(program, context);
    CHECK(program->count == 2 && program->records[0].type == mwin_eventDragEntered &&
              program->records[0].data.drag.contents == mwin_dragFiles &&
              program->records[1].type == mwin_eventDragLeft,
          "a drag of neither not reported, and one that leaves");
}

static void DropFiles(Program* program, mwinContext* context, HWND hwnd, float scale)
{
    CHECK((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0,
          "the window accepts dropped files");
    SendMessageW(hwnd, WM_DROPFILES, (WPARAM)Files((POINT){15, 25}), 0);
    Collect(program, context);
    CHECK(program->count == 1 && At(&program->records[0], mwin_eventDropped, scale, 15, 25) &&
              program->records[0].data.drop.fileCount == 2 &&
              Paths(context, program->records[0].data.drop.drop),
          "files dropped through WM_DROPFILES");
}

static void Check(Program* program, mwinContext* context)
{
    const mwinWin32Platform* platform = context->backendData;
    mwinWin32Window* window = &platform->windows[program->window.index1 - 1];
    float scale = mwinWin32Scale(GetDpiForWindow(window->hwnd));
    CHECK(window->drop.registered == !program->fallback,
          program->fallback ? "no drop target without OLE" : "a drop target");
    if (program->fallback)
    {
        DropFiles(program, context, window->hwnd, scale);
    }
    else
    {
        Drag(program, context, &window->drop.target, window->hwnd, scale);
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == mwin_eventWindowCreated)
        {
            Check(program, context);
            program->done = true;
            return mwin_frameStop;
        }
    }
    return GetTickCount64() - program->startMs > DEADLINE_MS ? mwin_frameStop : mwin_frameContinue;
}

static void Run(Program* program)
{
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = program;
    CHECK(mwinRun(&def) == mwin_success && program->done, "the program runs");
}

int main(void)
{
    static Program program;
    Run(&program);
    // A multithreaded thread: OLE cannot start there.
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "a multithreaded thread");
    program = (Program){.fallback = true};
    Run(&program);
    CoUninitialize();
    return s_failures == 0 ? 0 : 1;
}
