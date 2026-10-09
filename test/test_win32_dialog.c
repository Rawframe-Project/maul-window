// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's file dialogs against Windows' own common item
// dialog, driven from the program's frames, which go on while it
// shows: a save with the name offered and the first filter's extension,
// in the folder given; an open of files, two typed in; a dialog
// cancelled; a folder chosen; a dialog closed when a later one replaces
// it, and when its window goes; and one closed when the program stops.

#include "test_harness.h"

#include "maul-window/dialog.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 30000u

typedef enum Phase
{
    phaseCreate,
    phaseSave,
    phaseOpen,
    phaseCancel,
    phaseFolder,
    phaseSupersede,
    phaseGone,
    phaseCreateAgain,
    phaseStop,
    phaseDone,
} Phase;

// What a phase does to its dialog.
typedef enum Action
{
    actionOk,
    actionType,
    actionTypeFolder,
    actionCancel,
    actionSupersede,
    actionDestroy,
    actionStop,
} Action;

typedef struct Program
{
    ULONGLONG startMs;
    Phase phase;
    mwinWindowId window;
    mwinRequestId create;
    mwinRequestId request;
    int outcome;
    bool acted;
    bool stopped;
    // The dialog a later one replaced, which must close.
    HWND replaced;
    // When OK was last pressed on a folder dialog, and how often:
    // Windows chooses the folder it shows at OK with nothing typed, and
    // opens a folder typed in; wine chooses one typed in.
    ULONGLONG pressedMs;
    int presses;
    // The temporary folder, as UTF-8 and UTF-16.
    char folder[MAX_PATH * 3];
    WCHAR wideFolder[MAX_PATH];
} Program;

static const mwinFileFilter s_filters[] = {{"Text", 4, "txt", 3}};

// What each phase does to its dialog, by phase.
static const Action s_actions[] = {actionOk,         actionOk,        actionType,    actionCancel,
                                   actionTypeFolder, actionSupersede, actionDestroy, actionOk,
                                   actionStop,       actionOk};

typedef struct Found
{
    HWND owner;
    HWND dialog;
    HWND edit;
} Found;

static BOOL CALLBACK FindEdit(HWND hwnd, LPARAM data)
{
    Found* found = (Found*)data;
    WCHAR name[16];
    if (GetClassNameW(hwnd, name, 16) > 0 && wcscmp(name, L"Edit") == 0 && IsWindowVisible(hwnd))
    {
        found->edit = hwnd;
        return FALSE;
    }
    return TRUE;
}

// The dialog: a visible window of the thread's, owned by the program's.
static BOOL CALLBACK FindDialog(HWND hwnd, LPARAM data)
{
    Found* found = (Found*)data;
    if (hwnd != found->owner && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == found->owner)
    {
        found->dialog = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND OwnerOf(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (HWND)handles.handles.win32.hwnd
               : nullptr;
}

static void Ask(mwinContext* context, Program* program, mwinDialogKind kind);

// Acts on the dialog once it shows: true once it did.
static bool Act(mwinContext* context, Program* program)
{
    Found found = {OwnerOf(context, program->window), nullptr, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), FindDialog, (LPARAM)&found);
    if (found.dialog == nullptr)
    {
        return false;
    }
    switch (s_actions[program->phase])
    {
    case actionType:
    case actionTypeFolder:
    {
        EnumChildWindows(found.dialog, FindEdit, (LPARAM)&found);
        if (found.edit == nullptr)
        {
            return false;
        }
        // The folder by its path, without the last separator.
        WCHAR folder[MAX_PATH];
        (void)swprintf(folder, MAX_PATH, L"%ls", program->wideFolder);
        folder[wcslen(folder) - 1] = L'\0';
        bool file = s_actions[program->phase] == actionType;
        if (!file && program->presses > 0 && GetTickCount64() - program->pressedMs < 1000)
        {
            return false;
        }
        bool typed = file || program->presses % 2 == 1;
        SetWindowTextW(found.edit, file    ? L"\"mwin-open.txt\" \"mwin-two.txt\""
                                   : typed ? folder
                                           : L"");
        PostMessageW(found.dialog, WM_COMMAND, IDOK, 0);
        program->pressedMs = GetTickCount64();
        program->presses += 1;
        return file;
    }
    case actionCancel:
        PostMessageW(found.dialog, WM_COMMAND, IDCANCEL, 0);
        break;
    case actionSupersede:
        // A second dialog while the first shows, which the backend closes;
        // the second is cancelled once it shows instead.
        if (program->replaced == nullptr)
        {
            program->replaced = found.dialog;
            Ask(context, program, mwin_dialogOpen);
            return false;
        }
        if (found.dialog == program->replaced && IsWindow(found.dialog))
        {
            return false;
        }
        PostMessageW(found.dialog, WM_COMMAND, IDCANCEL, 0);
        break;
    case actionDestroy:
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "destroy");
        break;
    case actionStop:
        program->stopped = true;
        break;
    default:
        PostMessageW(found.dialog, WM_COMMAND, IDOK, 0);
        break;
    }
    return true;
}

static void Ask(mwinContext* context, Program* program, mwinDialogKind kind)
{
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = kind;
    def.title = "Maul";
    def.titleLength = 4;
    def.folder = program->folder;
    def.folderLength = strlen(program->folder);
    // Without its extension, which the first filter gives.
    def.name = "out";
    def.nameLength = 3;
    def.filters = s_filters;
    def.filterCount = kind == mwin_dialogFolder ? 0 : 1;
    program->outcome = -1;
    program->acted = false;
    CHECK(mwinRequestFileDialog(context, program->window, &def, &program->request) == mwin_success,
          "ask for a dialog");
}

// Whether the dialog chose these paths in the temporary folder, each
// once, in any order.
static bool Chose(mwinContext* context, const Program* program, const char* const* names,
                  uint32_t wanted)
{
    char paths[MAX_PATH * 6];
    size_t length = 0;
    uint32_t count = 0;
    bool got = mwinGetDialogFiles(context, program->request, paths, sizeof(paths), &length,
                                  &count) == mwin_success;
    bool same = got && count == wanted;
    size_t at = 0;
    uint32_t seen = 0;
    for (uint32_t i = 0; same && i < count; i++)
    {
        const char* path = paths + at;
        uint32_t match = wanted;
        for (uint32_t j = 0; j < wanted; j++)
        {
            char expected[MAX_PATH * 3];
            (void)snprintf(expected, sizeof(expected), "%s%s", program->folder, names[j]);
            match = (seen & (1u << j)) == 0 && _stricmp(path, expected) == 0 ? j : match;
        }
        same = match < wanted;
        seen |= same ? 1u << match : 0u;
        at += strlen(path) + 1;
    }
    same = same && length == at;
    if (!same)
    {
        (void)printf("expected %u paths in %s, got %s (%u paths)\n", (unsigned)wanted,
                     program->folder, got ? paths : "none", (unsigned)count);
    }
    return same;
}

static void Check(mwinContext* context, Program* program)
{
    (void)printf("phase %d: outcome %d\n", (int)program->phase, program->outcome);
    switch (program->phase)
    {
    case phaseSave:
        CHECK(program->outcome == mwin_outcomeDone &&
                  Chose(context, program, (const char* const[]){"out.txt"}, 1),
              "a save with the name offered and the first filter's extension, in the folder given");
        break;
    case phaseOpen:
        CHECK(
            program->outcome == mwin_outcomeDone &&
                Chose(context, program, (const char* const[]){"mwin-open.txt", "mwin-two.txt"}, 2),
            "an open of files, two typed in");
        break;
    case phaseCancel:
        CHECK(program->outcome == mwin_outcomeCancelled, "a dialog cancelled");
        break;
    case phaseFolder:
    {
        char trimmed[MAX_PATH * 3];
        (void)snprintf(trimmed, sizeof(trimmed), "%s", program->folder);
        trimmed[strlen(trimmed) - 1] = '\0';
        char paths[MAX_PATH * 3];
        size_t length = 0;
        bool chosen = program->outcome == mwin_outcomeDone &&
                      mwinGetDialogFiles(context, program->request, paths, sizeof(paths), &length,
                                         nullptr) == mwin_success &&
                      _stricmp(paths, trimmed) == 0;
        if (!chosen)
        {
            (void)printf("folder dialog: outcome %d, chose \"%.*s\", expected \"%s\"\n",
                         (int)program->outcome, (int)length, paths, trimmed);
        }
        CHECK(chosen, "a folder chosen");
        break;
    }
    case phaseSupersede:
        CHECK(program->outcome == mwin_outcomeCancelled && program->replaced != nullptr &&
                  !IsWindow(program->replaced),
              "a dialog closed when a later one replaces it");
        break;
    case phaseGone:
        CHECK(program->outcome == mwin_outcomeCancelled, "a dialog closed when its window goes");
        break;
    default:
        break;
    }
}

// Asks for the next phase's dialog, or its window.
static void Next(mwinContext* context, Program* program)
{
    static const mwinDialogKind kinds[] = {
        mwin_dialogSave, mwin_dialogSave, mwin_dialogOpenMany, mwin_dialogOpen, mwin_dialogFolder,
        mwin_dialogOpen, mwin_dialogOpen, mwin_dialogOpen,     mwin_dialogOpen, mwin_dialogOpen};
    program->phase += 1;
    if (program->phase == phaseCreateAgain)
    {
        mwinWindowDef def = mwinDefaultWindowDef();
        def.size = (mwinSize){320.0f, 200.0f};
        CHECK(mwinCreateWindow(context, &def, &program->window, &program->create) == mwin_success,
              "a window again");
    }
    else if (program->phase < phaseDone)
    {
        Ask(context, program, kinds[program->phase]);
    }
}

static void Drain(mwinContext* context, Program* program)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type != mwin_eventRequestCompleted)
        {
            continue;
        }
        if (completion->request.index1 == program->create.index1 &&
            completion->request.generation == program->create.generation)
        {
            program->create = (mwinRequestId){0};
            Next(context, program);
        }
        else if (completion->request.index1 == program->request.index1 &&
                 completion->request.generation == program->request.generation)
        {
            program->outcome = completion->outcome;
        }
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    return mwinCreateWindow(context, &def, &program->window, &program->create);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Drain(context, program);
    bool asking = program->phase > phaseCreate && program->phase != phaseCreateAgain &&
                  program->phase < phaseDone;
    if (asking && !program->acted)
    {
        program->acted = Act(context, program);
    }
    if (asking && program->outcome >= 0)
    {
        Check(context, program);
        Next(context, program);
    }
    bool late = GetTickCount64() - program->startMs > DEADLINE_MS;
    return program->stopped || late ? mwin_frameStop : mwin_frameContinue;
}

static void Touch(const WCHAR* folder, const WCHAR* name, bool make)
{
    WCHAR path[MAX_PATH * 2];
    (void)swprintf(path, MAX_PATH * 2, L"%ls%ls", folder, name);
    (void)DeleteFileW(path);
    if (make)
    {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        CloseHandle(file);
    }
}

int main(void)
{
    static Program program;
    // The long form: the one a dialog answers with.
    WCHAR temporary[MAX_PATH];
    (void)GetTempPathW(MAX_PATH, temporary);
    DWORD units = GetLongPathNameW(temporary, program.wideFolder, MAX_PATH);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, program.wideFolder, (int)units, program.folder,
                                    sizeof(program.folder) - 1, nullptr, nullptr);
    program.folder[bytes] = '\0';
    Touch(program.wideFolder, L"out.txt", false);
    Touch(program.wideFolder, L"mwin-open.txt", true);
    Touch(program.wideFolder, L"mwin-two.txt", true);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs");
    (void)printf("ended in phase %d\n", (int)program.phase);
    CHECK(program.phase == phaseStop && program.stopped,
          "every phase ran, and a dialog closed when the program stops");
    Touch(program.wideFolder, L"mwin-open.txt", false);
    Touch(program.wideFolder, L"mwin-two.txt", false);
    return s_failures == 0 ? 0 : 1;
}
