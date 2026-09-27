// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's touch and pen against Windows, driven through
// touch injection and a synthetic pen: a touch down, moved and up with
// one id and its pressure, and a pen hovering, its barrel button, down
// with pressure and tilt, and up. Where Windows injects neither (wine),
// the test is skipped (exit status 77). Injection now and then loses a
// gesture, the first after a window shows most of all, so a gesture
// that brings nothing is sent again.

#define _WIN32_WINNT  0x0A00
#define WINVER        0x0A00
#define NTDDI_VERSION 0x0A000006

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/native.h"

#define WIN32_LEAN_AND_MEAN
#include <string.h>
#include <windows.h>

#define DEADLINE_MS 10000u
#define RESEND_MS   1000u
#define MAX_RECORDS 64

// The synthetic pen, Windows 10 1809's, found at run time: mingw's
// import library lacks it and wine does not have it.
typedef HSYNTHETICPOINTERDEVICE(WINAPI* CreateFunction)(POINTER_INPUT_TYPE, ULONG,
                                                        POINTER_FEEDBACK_MODE);
typedef BOOL(WINAPI* InjectFunction)(HSYNTHETICPOINTERDEVICE, const POINTER_TYPE_INFO*, UINT32);
typedef void(WINAPI* DestroyFunction)(HSYNTHETICPOINTERDEVICE);

typedef enum Phase
{
    phaseCreate,
    phaseTouch,
    phasePen,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    ULONGLONG startMs;
    ULONGLONG sentMs;
    mwinWindowId window;
    HWND hwnd;
    HSYNTHETICPOINTERDEVICE pen;
    InjectFunction inject;
    mwinEvent records[MAX_RECORDS];
    int count;
    bool timedOut;
} Program;

// The pointer messages the thread took, for a failure's report.
static int s_pointerMessages = 0;

static LRESULT CALLBACK OnMessage(int code, WPARAM wParam, LPARAM lParam)
{
    const MSG* message = (const MSG*)lParam;
    if (code == HC_ACTION && wParam == PM_REMOVE && message->message >= WM_NCPOINTERUPDATE &&
        message->message <= WM_POINTERROUTEDRELEASED)
    {
        s_pointerMessages += 1;
        (void)printf("    message 0x%x, pointer %u\n", message->message,
                     (unsigned)GET_POINTERID_WPARAM(message->wParam));
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// A point of the client area, in pixels from its top left, on screen.
static POINT OnScreen(const Program* program, LONG x, LONG y)
{
    POINT point = {x, y};
    ClientToScreen(program->hwnd, &point);
    return point;
}

// What a gesture brought, for a failure's report.
static void Dump(const Program* program)
{
    POINT point = OnScreen(program, 100, 80);
    (void)printf("  phase %d: %d records, %d pointer messages; under the point %p, the window "
                 "%p, foreground %p\n",
                 (int)program->phase, program->count, s_pointerMessages,
                 (void*)WindowFromPoint(point), (void*)program->hwnd, (void*)GetForegroundWindow());
    for (int i = 0; i < program->count; i++)
    {
        (void)printf("    record %d\n", (int)program->records[i].type);
    }
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        program->records[program->count++] = event;
    }
}

static const mwinEvent* Find(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

// Whether a position in logical units is a pixel of the client area.
static bool At(const Program* program, mwinPosition position, float x, float y)
{
    float scale = (float)GetDpiForWindow(program->hwnd) / (float)USER_DEFAULT_SCREEN_DPI;
    return position.x * scale > x - 1.0f && position.x * scale < x + 1.0f &&
           position.y * scale > y - 1.0f && position.y * scale < y + 1.0f;
}

static void Touch(const Program* program, LONG x, LONG y, POINTER_FLAGS flags)
{
    POINTER_TOUCH_INFO contact = {0};
    contact.pointerInfo.pointerType = PT_TOUCH;
    contact.pointerInfo.ptPixelLocation = OnScreen(program, x, y);
    contact.pointerInfo.pointerFlags = flags;
    contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    POINT at = contact.pointerInfo.ptPixelLocation;
    contact.rcContact = (RECT){at.x - 2, at.y - 2, at.x + 2, at.y + 2};
    contact.orientation = 90;
    contact.pressure = 512;
    if (!InjectTouchInput(1, &contact))
    {
        (void)printf("  touch not taken: error %lu\n", GetLastError());
    }
    // A frame apart, as a digitizer reports.
    Sleep(16);
}

static void Pen(const Program* program, LONG x, LONG y, POINTER_FLAGS flags, PEN_FLAGS pen)
{
    POINTER_TYPE_INFO info = {.type = PT_PEN};
    info.penInfo.pointerInfo.pointerType = PT_PEN;
    info.penInfo.pointerInfo.ptPixelLocation = OnScreen(program, x, y);
    info.penInfo.pointerInfo.pointerFlags = flags;
    info.penInfo.penFlags = pen;
    info.penInfo.penMask = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
    info.penInfo.pressure = (flags & POINTER_FLAG_INCONTACT) != 0 ? 256 : 0;
    info.penInfo.tiltX = 30;
    info.penInfo.tiltY = -15;
    if (!program->inject(program->pen, &info, 1))
    {
        (void)printf("  pen not taken: error %lu\n", GetLastError());
    }
    Sleep(16);
}

// A touch down, moved and lifted; a pen hovering with its barrel button
// held, down, and lifted with it let go.
static void SendGesture(Program* program)
{
    const POINTER_FLAGS contact = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
    const POINTER_FLAGS hover = POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE;
    program->count = 0;
    program->sentMs = GetTickCount64();
    if (program->phase == phaseTouch)
    {
        Touch(program, 100, 80, POINTER_FLAG_DOWN | contact);
        Touch(program, 120, 90, POINTER_FLAG_UPDATE | contact);
        Touch(program, 120, 90, POINTER_FLAG_UP);
    }
    else
    {
        Pen(program, 200, 150, hover, PEN_FLAG_BARREL);
        Pen(program, 200, 150, POINTER_FLAG_DOWN | contact, PEN_FLAG_BARREL);
        Pen(program, 200, 150, POINTER_FLAG_UP | POINTER_FLAG_INRANGE, PEN_FLAG_NONE);
        Pen(program, 200, 150, POINTER_FLAG_UPDATE, PEN_FLAG_NONE);
    }
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventShown) != nullptr;
    case phaseTouch:
        return Find(program, mwin_eventTouchUp) != nullptr ||
               Find(program, mwin_eventTouchCancelled) != nullptr;
    default:
        return Find(program, mwin_eventPenUp) != nullptr;
    }
}

static void CheckTouch(const Program* program)
{
    const mwinEvent* down = Find(program, mwin_eventTouchDown);
    const mwinEvent* moved = Find(program, mwin_eventTouchMoved);
    const mwinEvent* up = Find(program, mwin_eventTouchUp);
    CHECK(down != nullptr && At(program, down->data.touch.position, 100.0f, 80.0f) &&
              down->data.touch.pressure == 0.5f,
          "a touch down where it lands, with its pressure");
    CHECK(down != nullptr && moved != nullptr && moved->data.touch.id == down->data.touch.id &&
              At(program, moved->data.touch.position, 120.0f, 90.0f),
          "the touch moves, the same id");
    CHECK(down != nullptr && up != nullptr && up->data.touch.id == down->data.touch.id,
          "the touch lifts, the same id");
    CHECK(Find(program, mwin_eventButtonDown) == nullptr, "no mouse made of the touch");
}

static void CheckPen(const Program* program)
{
    const mwinEvent* pressed = Find(program, mwin_eventPenButtonDown);
    const mwinEvent* down = Find(program, mwin_eventPenDown);
    const mwinEvent* up = Find(program, mwin_eventPenUp);
    CHECK(pressed != nullptr && pressed->data.pen.button == 1 &&
              (pressed->data.pen.flags & mwin_penContact) == 0 &&
              At(program, pressed->data.pen.position, 200.0f, 150.0f),
          "the barrel button, the pen hovering");
    CHECK(down != nullptr && down->data.pen.pressure == 0.25f && down->data.pen.tiltX == 30.0f &&
              down->data.pen.tiltY == -15.0f &&
              down->data.pen.flags == (mwin_penContact | mwin_penBarrel),
          "the pen down with its pressure and tilt");
    CHECK(Find(program, mwin_eventPenButtonUp) != nullptr &&
              (up->data.pen.flags & mwin_penContact) == 0,
          "the pen up, its button let go");
    CHECK(Find(program, mwin_eventButtonDown) == nullptr, "no mouse made of the pen");
}

static void Advance(Program* program, mwinContext* context)
{
    if (program->phase == phaseCreate)
    {
        mwinNativeHandles handles;
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success,
              "the window's HWND");
        program->hwnd = handles.handles.win32.hwnd;
        // Nothing of the desktop's may cover the points touched.
        SetWindowPos(program->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    else if (program->phase == phaseTouch)
    {
        CheckTouch(program);
    }
    else
    {
        CheckPen(program);
    }
    program->phase += 1;
    program->startMs = GetTickCount64();
    if (program->phase != phaseDone)
    {
        SendGesture(program);
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){640.0f, 480.0f};
    program->startMs = GetTickCount64();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    ULONGLONG now = GetTickCount64();
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (now - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        Dump(program);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else if (program->phase != phaseCreate && now - program->sentMs > RESEND_MS)
    {
        Dump(program);
        (void)printf("  phase %d: sent again\n", (int)program->phase);
        SendGesture(program);
    }
    else
    {
        Sleep(1);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

// A function of user32, its bytes copied: a FARPROC is no function of
// the right type to cast.
static void Load(const char* name, void* function, size_t size)
{
    FARPROC found = GetProcAddress(GetModuleHandleW(L"user32.dll"), name);
    memcpy(function, (const void*)&found, size);
}

int main(void)
{
    Program program = {0};
    CreateFunction create = nullptr;
    DestroyFunction destroy = nullptr;
    Load("CreateSyntheticPointerDevice", (void*)&create, sizeof(create));
    Load("DestroySyntheticPointerDevice", (void*)&destroy, sizeof(destroy));
    Load("InjectSyntheticPointerInput", (void*)&program.inject, sizeof(program.inject));
    if (create == nullptr || destroy == nullptr || program.inject == nullptr ||
        !InitializeTouchInjection(1, TOUCH_FEEDBACK_NONE))
    {
        return 77;
    }
    program.pen = create(PT_PEN, 1, POINTER_FEEDBACK_NONE);
    if (program.pen == nullptr)
    {
        return 77;
    }
    HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, OnMessage, nullptr, GetCurrentThreadId());
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on Windows");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    destroy(program.pen);
    UnhookWindowsHookEx(hook);
    return s_failures == 0 ? 0 : 1;
}
