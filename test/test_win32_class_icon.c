// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A window given no icon shows the program's own, as its file does in
// Explorer: the class's big and small icons are the program's first icon
// group at the sizes Windows shows them, read back in its one colour, and
// the window has none of its own.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 10000u

// The icon's colour, BGRA as Windows reads it back (win32_class_icon.ico).
static const uint8_t s_color[4] = {200, 120, 40, 255};

typedef struct Program
{
    ULONGLONG startMs;
    mwinWindowId window;
    mwinRequestId request;
    bool done;
} Program;

static bool Created(mwinContext* context, mwinRequestId request)
{
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == request.index1 &&
            completion->request.generation == request.generation)
        {
            return true;
        }
    }
    return false;
}

// Whether an icon is the program's at a size: that side, its first pixel
// the icon's colour.
static bool Shows(HICON icon, int side)
{
    ICONINFO info;
    if (icon == nullptr || !GetIconInfo(icon, &info))
    {
        return false;
    }
    BITMAPINFO header = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER),
                                       .biWidth = side,
                                       .biHeight = -side,
                                       .biPlanes = 1,
                                       .biBitCount = 32,
                                       .biCompression = BI_RGB}};
    static uint8_t bits[64 * 64 * 4];
    BITMAP bitmap;
    HDC screen = GetDC(nullptr);
    bool sized = side <= 64 && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) != 0 &&
                 bitmap.bmWidth == side && bitmap.bmHeight == side;
    bool read = sized && GetDIBits(screen, info.hbmColor, 0, (UINT)side, bits, &header,
                                   DIB_RGB_COLORS) == side;
    ReleaseDC(nullptr, screen);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    return read && bits[0] == s_color[0] && bits[1] == s_color[1] && bits[2] == s_color[2] &&
           bits[3] == s_color[3];
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startMs = GetTickCount64();
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){320.0f, 200.0f};
    return mwinCreateWindow(context, &def, &program->window, &program->request);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    if (!program->done && Created(context, program->request))
    {
        mwinNativeHandles handles;
        CHECK(mwinWindow_GetNativeHandles(context, program->window, &handles) == mwin_success,
              "the window's handle");
        HWND hwnd = (HWND)handles.handles.win32.hwnd;
        HICON bigIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
        HICON smallIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
        CHECK(Shows(bigIcon, GetSystemMetrics(SM_CXICON)) &&
                  Shows(smallIcon, GetSystemMetrics(SM_CXSMICON)),
              "the class's big and small icons the program's, at the sizes Windows shows");
        CHECK(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == 0,
              "the window has no icon of its own");
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "destroy");
        program->done = true;
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
