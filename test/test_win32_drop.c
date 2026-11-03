// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's drag and drop, its drop target driven as OLE
// would drive it, with a data object of the test's: a drag of files and
// text reported where it enters and moves, a drag over that has not
// moved not reported again, and a drop delivering the paths, a path
// with a lone surrogate left out, and the text repaired, its memory left
// unlocked; a drag of neither refused and not reported; a drag that
// leaves; no interface but the target's own; a data object answering
// with a stream where global memory was asked, its stream let go unread
// and the drop truncated; a success other than S_OK taken, its text read
// and its stream let go. Then, with the thread made multithreaded first
// so OLE cannot start, files dropped through WM_DROPFILES, a drop of good
// paths alone not truncated, one of more paths than a drop delivers
// truncated to them, bad paths ahead of good ones passed over until a
// drop's worth, paths in the system's code page, and a list that starts
// past its memory read as none.

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
    // The text's memory, lent rather than given: its release is the
    // object's, so the test sees whether it was left locked.
    HGLOBAL lent;
    // It answers with a stream, of another medium than asked for.
    bool stream;
    // What GetData answers when it holds the format: S_OK, or another
    // success.
    HRESULT answer;
} Data;

// A stream that is only ever let go, and counts how often.
typedef struct Stream
{
    IStream stream;
    int releases;
} Stream;

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

static HRESULT STDMETHODCALLTYPE StreamQuery(IStream* stream, REFIID id, void** out)
{
    (void)stream;
    (void)id;
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE StreamReference(IStream* stream)
{
    (void)stream;
    return 1;
}

static ULONG STDMETHODCALLTYPE StreamRelease(IStream* stream)
{
    ((Stream*)(void*)stream)->releases += 1;
    return 1;
}

static const IStreamVtbl s_stream = {
    .QueryInterface = StreamQuery, .AddRef = StreamReference, .Release = StreamRelease};
static Stream s_answered = {{&s_stream}, 0};

static HRESULT STDMETHODCALLTYPE DataGet(IDataObject* object, FORMATETC* format, STGMEDIUM* medium)
{
    static const WCHAR text[] = {L'h', L'i', 0xD800, L'!', 0};
    if (!Holds(DataOf(object), format))
    {
        return DV_E_FORMATETC;
    }
    if (DataOf(object)->stream)
    {
        *medium = (STGMEDIUM){.tymed = TYMED_ISTREAM, .pstm = &s_answered.stream};
        return DataOf(object)->answer;
    }
    *medium = (STGMEDIUM){.tymed = TYMED_HGLOBAL};
    if (format->cfFormat == CF_HDROP)
    {
        medium->hGlobal = Files((POINT){0, 0});
        return S_OK;
    }
    medium->hGlobal = Global(text, sizeof(text), 0);
    medium->pUnkForRelease = (IUnknown*)(void*)object;
    DataOf(object)->lent = medium->hGlobal;
    return DataOf(object)->answer;
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
    while (mwinContext_NextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
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

static bool At(const mwinEvent* event, mwinEventKind type, float scale, LONG x, LONG y)
{
    mwinPosition position =
        type == mwin_eventDropped ? event->data.drop.position : event->data.drag.position;
    return event->kind == type && position.x == (float)x / scale && position.y == (float)y / scale;
}

static bool Paths(mwinContext* context, mwinDropId drop)
{
    char paths[64];
    size_t length = 0;
    static const char expected[] = "C:\\a.txt\0C:\\\xC3\xA9.png";
    return mwinDrop_GetFiles(context, drop, paths, sizeof(paths), &length) == mwin_success &&
           length == sizeof(expected) && memcmp(paths, expected, length) == 0;
}

static void Drag(Program* program, mwinContext* context, IDropTarget* target, HWND hwnd,
                 float scale)
{
    Data both = {{&s_data}, true, true, nullptr, false, S_OK};
    Data neither = {{&s_data}, false, false, nullptr, false, S_OK};
    Data files = {{&s_data}, true, false, nullptr, false, S_OK};
    Data stream = {{&s_data}, false, true, nullptr, true, S_OK};
    Data sameText = {{&s_data}, false, true, nullptr, false, DATA_S_SAMEFORMATETC};
    Data sameStream = {{&s_data}, false, true, nullptr, true, DATA_S_SAMEFORMATETC};
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
              mwinDrop_GetText(context, drop->drop, text, sizeof(text), &length) == mwin_success &&
              length == 6 && memcmp(text, "hi\xEF\xBF\xBD!", 6) == 0,
          "the paths, one with a lone surrogate left out, and the text repaired");
    CHECK(both.lent != nullptr && (GlobalFlags(both.lent) & GMEM_LOCKCOUNT) == 0,
          "the text's memory left unlocked");
    GlobalFree(both.lent);
    void* other = &effect;
    void* self = nullptr;
    CHECK(target->lpVtbl->QueryInterface(target, &IID_IDataObject, &other) == E_NOINTERFACE &&
              other == nullptr &&
              target->lpVtbl->QueryInterface(target, &IID_IDropTarget, &self) == S_OK &&
              self == target,
          "no interface but the target's own");
    effect = DROPEFFECT_COPY;
    target->lpVtbl->DragEnter(target, &neither.object, 0, Screen(hwnd, 5, 5), &effect);
    target->lpVtbl->DragLeave(target);
    CHECK(effect == DROPEFFECT_NONE, "a drag of neither refused");
    target->lpVtbl->DragEnter(target, &files.object, 0, Screen(hwnd, 5, 5), &effect);
    target->lpVtbl->DragLeave(target);
    Collect(program, context);
    CHECK(program->count == 2 && program->records[0].kind == mwin_eventDragEntered &&
              program->records[0].data.drag.contents == mwin_dragFiles &&
              program->records[1].kind == mwin_eventDragLeft,
          "a drag of neither not reported, and one that leaves");
    target->lpVtbl->DragEnter(target, &stream.object, 0, Screen(hwnd, 5, 5), &effect);
    target->lpVtbl->Drop(target, &stream.object, 0, Screen(hwnd, 5, 5), &effect);
    Collect(program, context);
    CHECK(program->count == 2 && program->records[1].kind == mwin_eventDropped &&
              program->records[1].data.drop.textLength == 0 &&
              program->records[1].data.drop.truncated && s_answered.releases == 1,
          "a stream where global memory was asked let go unread, and the drop truncated");
    target->lpVtbl->DragEnter(target, &sameText.object, 0, Screen(hwnd, 5, 5), &effect);
    target->lpVtbl->Drop(target, &sameText.object, 0, Screen(hwnd, 5, 5), &effect);
    target->lpVtbl->DragEnter(target, &sameStream.object, 0, Screen(hwnd, 5, 5), &effect);
    target->lpVtbl->Drop(target, &sameStream.object, 0, Screen(hwnd, 5, 5), &effect);
    Collect(program, context);
    CHECK(program->count == 4 && program->records[1].kind == mwin_eventDropped &&
              program->records[1].data.drop.textLength == 6 &&
              program->records[3].kind == mwin_eventDropped && s_answered.releases == 2,
          "a success other than S_OK taken: its text read, its stream let go");
    CHECK(sameText.lent != nullptr && (GlobalFlags(sameText.lent) & GMEM_LOCKCOUNT) == 0,
          "that text's memory left unlocked");
    GlobalFree(sameText.lent);
}

// Drops a list of paths through WM_DROPFILES, its header saying where it
// starts and whether it is UTF-16; the drop's record.
static mwinDropEvent Send(Program* program, mwinContext* context, HWND hwnd, const void* list,
                          size_t size, DWORD start, BOOL wide)
{
    HGLOBAL memory = Global(list, size, sizeof(DROPFILES));
    DROPFILES* header = GlobalLock(memory);
    *header = (DROPFILES){.pFiles = start, .pt = {15, 25}, .fWide = wide};
    GlobalUnlock(memory);
    SendMessageW(hwnd, WM_DROPFILES, (WPARAM)memory, 0);
    Collect(program, context);
    return program->count == 1 && program->records[0].kind == mwin_eventDropped
               ? program->records[0].data.drop
               : (mwinDropEvent){0};
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
    static const WCHAR good[] = L"C:\\a.txt\0";
    HGLOBAL memory = Global(good, sizeof(good), sizeof(DROPFILES));
    DROPFILES* header = GlobalLock(memory);
    *header = (DROPFILES){.pFiles = sizeof(DROPFILES), .pt = {15, 25}, .fWide = TRUE};
    GlobalUnlock(memory);
    SendMessageW(hwnd, WM_DROPFILES, (WPARAM)memory, 0);
    Collect(program, context);
    CHECK(program->count == 1 && program->records[0].data.drop.fileCount == 1 &&
              !program->records[0].data.drop.truncated,
          "a drop of good paths alone not truncated");
    // One path more than a drop delivers, each "C:\\a" and its end, and
    // the list's end.
    static WCHAR many[257 * 5 + 1];
    for (size_t i = 0; i < 257; i++)
    {
        memcpy(&many[i * 5], L"C:\\a", 5 * sizeof(WCHAR));
    }
    memory = Global(many, sizeof(many), sizeof(DROPFILES));
    header = GlobalLock(memory);
    *header = (DROPFILES){.pFiles = sizeof(DROPFILES), .pt = {15, 25}, .fWide = TRUE};
    GlobalUnlock(memory);
    SendMessageW(hwnd, WM_DROPFILES, (WPARAM)memory, 0);
    Collect(program, context);
    CHECK(program->count == 1 && program->records[0].data.drop.fileCount == 256 &&
              program->records[0].data.drop.truncated,
          "a drop of more paths than a drop delivers truncated to them");
    // Ten paths with a lone surrogate, each "C:\\b", U+D800 and its end,
    // then 290 good ones.
    static WCHAR mixed[10 * 6 + 290 * 5 + 1];
    for (size_t i = 0; i < 10; i++)
    {
        memcpy(&mixed[i * 6], L"C:\\b\xD800", 6 * sizeof(WCHAR));
    }
    for (size_t i = 0; i < 290; i++)
    {
        memcpy(&mixed[10 * 6 + i * 5], L"C:\\a", 5 * sizeof(WCHAR));
    }
    mwinDropEvent drop =
        Send(program, context, hwnd, mixed, sizeof(mixed), sizeof(DROPFILES), TRUE);
    CHECK(drop.fileCount == 256 && drop.truncated,
          "bad paths ahead of good ones passed over until a drop's worth");
    static const char narrow[] = "C:\\a.txt\0";
    drop = Send(program, context, hwnd, narrow, sizeof(narrow), sizeof(DROPFILES), FALSE);
    char paths[16];
    size_t length = 0;
    CHECK(drop.fileCount == 1 && !drop.truncated &&
              mwinDrop_GetFiles(context, drop.drop, paths, sizeof(paths), &length) ==
                  mwin_success &&
              length == 9 && memcmp(paths, "C:\\a.txt", 9) == 0,
          "paths in the system's code page");
    drop = Send(program, context, hwnd, good, sizeof(good), sizeof(DROPFILES) + 4096, TRUE);
    CHECK(drop.fileCount == 0 && drop.truncated, "a list that starts past its memory read as none");
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
        if (program->records[i].kind == mwin_eventWindowCreated)
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
