// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's window icons, read back from Windows: the big and
// small icons made from the images nearest the sizes Windows shows at
// the window's DPI, their colours and straight alpha kept; the icons
// the backend made destroyed when replaced, none left for the class's
// own; set and taken away ten times, no GDI object left behind (Windows
// counts them, wine reports none); and the last ones destroyed with the
// window.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 10000u

// Three images, each one colour: RGBA.
static const uint8_t s_colors[3][4] = {{255, 0, 0, 255}, {0, 255, 0, 128}, {0, 0, 255, 64}};
static const uint32_t s_sizes[3] = {16, 32, 48};
static uint8_t s_pixels[3][48 * 48 * 4];

typedef struct Program
{
    ULONGLONG startMs;
    mwinWindowId window;
    mwinRequestId request;
    int step;
    DWORD objects;
    HICON first;
    bool done;
} Program;

static int Outcome(mwinContext* context, mwinRequestId request)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type == mwin_eventRequestCompleted &&
            completion->request.index1 == request.index1 &&
            completion->request.generation == request.generation)
        {
            return completion->outcome;
        }
    }
    return -1;
}

static HWND Handle(mwinContext* context, mwinWindowId window)
{
    mwinNativeHandles handles;
    return mwinGetNativeHandles(context, window, &handles) == mwin_success
               ? (HWND)handles.handles.win32.hwnd
               : nullptr;
}

// The image Windows should get for a size: the smallest at least as
// large, else the largest.
static int Expected(int size)
{
    for (int i = 0; i < 3; i++)
    {
        if ((int)s_sizes[i] >= size)
        {
            return i;
        }
    }
    return 2;
}

// Whether an icon is the image of a size, read back as BGRA.
static bool Shows(HICON icon, int size)
{
    ICONINFO info;
    if (icon == nullptr || !GetIconInfo(icon, &info))
    {
        return false;
    }
    int image = Expected(size);
    uint32_t side = s_sizes[image];
    BITMAPINFO header = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER),
                                       .biWidth = (LONG)side,
                                       .biHeight = -(LONG)side,
                                       .biPlanes = 1,
                                       .biBitCount = 32,
                                       .biCompression = BI_RGB}};
    static uint8_t bits[48 * 48 * 4];
    BITMAP bitmap;
    HDC screen = GetDC(nullptr);
    bool sized = GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) != 0 &&
                 bitmap.bmWidth == (LONG)side && bitmap.bmHeight == (LONG)side;
    bool read = sized && GetDIBits(screen, info.hbmColor, 0, side, bits, &header, DIB_RGB_COLORS) ==
                             (int)side;
    ReleaseDC(nullptr, screen);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    const uint8_t* color = s_colors[image];
    return read && bits[0] == color[2] && bits[1] == color[1] && bits[2] == color[0] &&
           bits[3] == color[3];
}

static bool Destroyed(HICON icon)
{
    ICONINFO info;
    if (GetIconInfo(icon, &info))
    {
        DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
        return false;
    }
    return true;
}

static void Ask(mwinContext* context, Program* program, uint32_t count)
{
    mwinIconImage images[3];
    for (uint32_t i = 0; i < 3; i++)
    {
        images[i] = (mwinIconImage){s_sizes[i], s_sizes[i], s_sizes[i] * 4, s_pixels[i]};
    }
    CHECK(mwinRequestIcon(context, program->window, images, count, &program->request) ==
              mwin_success,
          "ask for an icon");
}

// Icons set and taken away again after the first ones.
#define CYCLES 10
#define LAST   (3 + 2 * CYCLES - 1)

static DWORD Objects(void)
{
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

static void Check(mwinContext* context, Program* program)
{
    HWND hwnd = Handle(context, program->window);
    UINT dpi = GetDpiForWindow(hwnd);
    HICON bigIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    HICON smallIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    switch (program->step)
    {
    case 1:
        CHECK(Shows(bigIcon, GetSystemMetricsForDpi(SM_CXICON, dpi)) &&
                  Shows(smallIcon, GetSystemMetricsForDpi(SM_CXSMICON, dpi)),
              "the big and small icons from the nearest images, colours and alpha kept");
        program->first = bigIcon;
        Ask(context, program, 0);
        break;
    case 2:
        CHECK(bigIcon == nullptr && smallIcon == nullptr && Destroyed(program->first),
              "the icons made destroyed when replaced, none left for the class's own");
        program->objects = Objects();
        Ask(context, program, 3);
        break;
    default:
        if (program->step < LAST)
        {
            Ask(context, program, program->step % 2 == 1 ? 0 : 3);
            break;
        }
        if (program->step == LAST)
        {
            // A leak leaves four bitmaps a cycle; Windows' own caches may
            // add a few.
            CHECK(bigIcon == nullptr && Objects() < program->objects + 2 * CYCLES,
                  "set and taken away again, no GDI object left behind");
            Ask(context, program, 3);
            break;
        }
        program->first = bigIcon;
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "destroy");
        CHECK(Destroyed(program->first), "the last icons destroyed with the window");
        program->done = true;
        break;
    }
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
    if (Outcome(context, program->request) >= 0)
    {
        if (program->step == 0)
        {
            Ask(context, program, 3);
        }
        else
        {
            Check(context, program);
        }
        program->step += 1;
    }
    bool late = GetTickCount64() - program->startMs > DEADLINE_MS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    for (int image = 0; image < 3; image++)
    {
        for (uint32_t i = 0; i < s_sizes[image] * s_sizes[image]; i++)
        {
            memcpy(&s_pixels[image][i * 4], s_colors[image], 4);
        }
    }
    static Program program;
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
