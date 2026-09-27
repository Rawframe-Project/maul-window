// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's touch and pen against Windows, driven through
// touch injection and a synthetic pen: a touch down, moved and up with
// one id and its pressure, and a pen hovering, its barrel button, down
// with pressure and tilt, and up. Where Windows injects neither (wine),
// the test is skipped (exit status 77).

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

// The synthetic pen, Windows 10 1809's, found at run time: mingw's
// import library lacks it and wine does not have it.
typedef HSYNTHETICPOINTERDEVICE(WINAPI* CreateFunction)(POINTER_INPUT_TYPE, ULONG,
                                                        POINTER_FEEDBACK_MODE);
typedef BOOL(WINAPI* InjectFunction)(HSYNTHETICPOINTERDEVICE, const POINTER_TYPE_INFO*, UINT32);
typedef void(WINAPI* DestroyFunction)(HSYNTHETICPOINTERDEVICE);
#define MAX_RECORDS 64

typedef enum Phase
{
    phaseCreate,
    phaseTouchDown,
    phaseTouchMove,
    phaseTouchUp,
    phasePenHover,
    phasePenDown,
    phasePenUp,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    ULONGLONG startMs;
    mwinWindowId window;
    HWND hwnd;
    HSYNTHETICPOINTERDEVICE pen;
    InjectFunction inject;
    bool touch;
    uint64_t touchId;
    mwinEvent records[MAX_RECORDS];
    int count;
    bool timedOut;
} Program;

// The pointer and mouse messages the thread took, for a timeout's report.
static int s_pointerMessages = 0;
static int s_mouseMessages = 0;

static LRESULT CALLBACK OnMessage(int code, WPARAM wParam, LPARAM lParam)
{
    const MSG* message = (const MSG*)lParam;
    if (code == HC_ACTION && wParam == PM_REMOVE)
    {
        s_pointerMessages +=
            message->message >= WM_NCPOINTERUPDATE && message->message <= WM_POINTERROUTEDRELEASED;
        s_mouseMessages += message->message >= WM_MOUSEFIRST && message->message <= WM_MOUSELAST;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
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

// A point of the client area, in pixels from its top left, on screen.
static POINT OnScreen(const Program* program, LONG x, LONG y)
{
    POINT point = {x, y};
    ClientToScreen(program->hwnd, &point);
    return point;
}

// The window's logical units per pixel.
static float Scale(const Program* program)
{
    return (float)GetDpiForWindow(program->hwnd) / (float)USER_DEFAULT_SCREEN_DPI;
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
    CHECK(InjectTouchInput(1, &contact), "Windows takes the touch");
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
    CHECK(program->inject(program->pen, &info, 1), "Windows takes the pen");
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventShown) != nullptr;
    case phaseTouchDown:
        return Find(program, mwin_eventTouchDown) != nullptr;
    case phaseTouchMove:
        return Find(program, mwin_eventTouchMoved) != nullptr;
    case phaseTouchUp:
        return Find(program, mwin_eventTouchUp) != nullptr;
    case phasePenHover:
        return Find(program, mwin_eventPenButtonDown) != nullptr;
    case phasePenDown:
        return Find(program, mwin_eventPenDown) != nullptr;
    default:
        return Find(program, mwin_eventPenUp) != nullptr;
    }
}

static bool At(const Program* program, mwinPosition position, float x, float y)
{
    // Injection is in whole pixels.
    float scale = Scale(program);
    return position.x * scale > x - 1.0f && position.x * scale < x + 1.0f &&
           position.y * scale > y - 1.0f && position.y * scale < y + 1.0f;
}

static void AdvanceTouch(Program* program)
{
    switch (program->phase)
    {
    case phaseTouchDown:
    {
        const mwinTouchEvent* down = &Find(program, mwin_eventTouchDown)->data.touch;
        CHECK(At(program, down->position, 100.0f, 80.0f) && down->pressure == 0.5f,
              "a touch down where it lands, with its pressure");
        program->touchId = down->id;
        Touch(program, 120, 90,
              POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
        break;
    }
    case phaseTouchMove:
    {
        const mwinTouchEvent* moved = &Find(program, mwin_eventTouchMoved)->data.touch;
        CHECK(moved->id == program->touchId && At(program, moved->position, 120.0f, 90.0f),
              "the touch moves, the same id");
        Touch(program, 120, 90, POINTER_FLAG_UP);
        break;
    }
    default:
        CHECK(Find(program, mwin_eventTouchUp)->data.touch.id == program->touchId &&
                  Find(program, mwin_eventButtonDown) == nullptr,
              "the touch lifts, no mouse made of it");
        break;
    }
}

static void AdvancePen(Program* program)
{
    switch (program->phase)
    {
    case phasePenHover:
    {
        const mwinPenEvent* pressed = &Find(program, mwin_eventPenButtonDown)->data.pen;
        CHECK(pressed->button == 1 && (pressed->flags & mwin_penContact) == 0 &&
                  At(program, pressed->position, 200.0f, 150.0f),
              "the barrel button, the pen hovering");
        Pen(program, 200, 150, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT,
            PEN_FLAG_BARREL);
        break;
    }
    case phasePenDown:
    {
        const mwinPenEvent* down = &Find(program, mwin_eventPenDown)->data.pen;
        CHECK(down->pressure == 0.25f && down->tiltX == 30.0f && down->tiltY == -15.0f &&
                  down->flags == (mwin_penContact | mwin_penBarrel),
              "the pen down with its pressure and tilt");
        Pen(program, 200, 150, POINTER_FLAG_UP | POINTER_FLAG_INRANGE, PEN_FLAG_NONE);
        break;
    }
    default:
        CHECK(Find(program, mwin_eventPenButtonUp) != nullptr &&
                  (Find(program, mwin_eventPenUp)->data.pen.flags & mwin_penContact) == 0,
              "the pen up, its button let go");
        break;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    if (program->phase == phaseCreate)
    {
        mwinNativeHandles handles;
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success,
              "the window's HWND");
        program->hwnd = handles.handles.win32.hwnd;
        SetForegroundWindow(program->hwnd);
        Touch(program, 100, 80, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
    }
    else if (program->phase <= phaseTouchUp)
    {
        AdvanceTouch(program);
    }
    else
    {
        AdvancePen(program);
    }
    if (program->phase == phaseTouchUp)
    {
        Pen(program, 200, 150, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE, PEN_FLAG_BARREL);
    }
    program->phase += 1;
    program->count = 0;
    program->startMs = GetTickCount64();
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
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (GetTickCount64() - program->startMs > DEADLINE_MS)
    {
        (void)printf("timed out in phase %d: %d pointer and %d mouse messages\n",
                     (int)program->phase, s_pointerMessages, s_mouseMessages);
        for (int i = 0; i < program->count; i++)
        {
            (void)printf("  record type %d\n", (int)program->records[i].type);
        }
        program->timedOut = true;
        return mwin_frameStop;
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
