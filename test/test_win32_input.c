// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend's input against Windows (a CI runner's desktop, or
// wine), driven through SendInput: keys with their codes, meanings and
// text, Shift, a repeat, text outside the BMP, the pointer entering and
// moving, a double click, the wheel, cursor shapes, a cursor made from
// images and its end, a hidden and confined cursor, and a captured one
// with raw motion.

#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/input.h"
#include "maul-window/native.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 10000u
#define MAX_RECORDS 256

// The keys by virtual key and scan code, as a keyboard sends them.
#define KEY_A     'A', 0x1E
#define KEY_SHIFT VK_LSHIFT, 0x2A

typedef enum Phase
{
    phaseCreate,
    phaseFocus,
    phaseKeys,
    phaseShift,
    phaseRepeat,
    phaseText,
    phasePointer,
    phaseClicks,
    phaseWheel,
    phaseShape,
    phaseImage,
    phaseShapeAgain,
    phaseImageAgain,
    phaseConfine,
    phaseRelease,
    phaseCapture,
    phaseRaw,
    phaseUncapture,
    phaseDone,
} Phase;

typedef struct Program
{
    Phase phase;
    ULONGLONG startMs;
    mwinWindowId window;
    HWND hwnd;
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[64];
    uint32_t textLength;
    bool entered;
    bool timedOut;
    mwinCursorId cursor;
} Program;

static void Send(INPUT input)
{
    CHECK(SendInput(1, &input, sizeof(input)) == 1, "SendInput takes the input");
}

static void Key(WORD key, WORD scan, bool down)
{
    INPUT input = {.type = INPUT_KEYBOARD};
    input.ki.wVk = key;
    input.ki.wScan = scan;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    Send(input);
}

static void Unit(WORD unit)
{
    INPUT input = {.type = INPUT_KEYBOARD};
    input.ki.wScan = unit;
    input.ki.dwFlags = KEYEVENTF_UNICODE;
    Send(input);
    input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    Send(input);
}

static void Mouse(DWORD flags, LONG dx, LONG dy, DWORD data)
{
    INPUT input = {.type = INPUT_MOUSE};
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.mouseData = data;
    input.mi.dwFlags = flags;
    Send(input);
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        if (event.type == mwin_eventTextInput &&
            program->textLength + event.data.text.length <= sizeof(program->text))
        {
            memcpy(program->text + program->textLength, event.data.text.text,
                   event.data.text.length);
            program->textLength += event.data.text.length;
        }
        program->entered |= event.type == mwin_eventCursorEntered;
        program->records[program->count++] = event;
    }
}

static const mwinEvent* Find(const Program* program, mwinEventType type, int nth)
{
    for (int i = 0; i < program->count; i++)
    {
        if (program->records[i].type == type && nth-- == 0)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return Find(program, mwin_eventShown, 0) != nullptr;
    case phaseFocus:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    case phaseKeys:
    case phaseShift:
        return Find(program, mwin_eventKeyUp, program->phase == phaseShift ? 1 : 0) != nullptr;
    case phaseRepeat:
        return Find(program, mwin_eventKeyUp, 0) != nullptr;
    case phaseText:
        return program->textLength >= 4;
    case phasePointer:
        return Find(program, mwin_eventCursorMoved, 0) != nullptr;
    case phaseClicks:
        return Find(program, mwin_eventButtonUp, 1) != nullptr;
    case phaseWheel:
        return Find(program, mwin_eventWheel, 0) != nullptr;
    case phaseRaw:
        return Find(program, mwin_eventRawPointerDelta, 0) != nullptr;
    default:
        return Find(program, mwin_eventRequestCompleted, 0) != nullptr;
    }
}

static bool TextIs(const Program* program, const char* text)
{
    return program->textLength == strlen(text) && memcmp(program->text, text, strlen(text)) == 0;
}

static RECT ClientOnScreen(HWND hwnd)
{
    RECT client;
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, nullptr, (POINT*)&client, 2);
    return client;
}

static bool ClipIs(RECT expected)
{
    RECT clip;
    return GetClipCursor(&clip) && EqualRect(&clip, &expected);
}

static RECT Desktop(void)
{
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return (RECT){x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                  y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

static mwinOutcome Outcome(const Program* program)
{
    const mwinEvent* completed = Find(program, mwin_eventRequestCompleted, 0);
    return completed != nullptr ? completed->data.completion.outcome : 0;
}

// The pointer's phases.
static void AdvancePointer(Program* program)
{
    switch (program->phase)
    {
    case phasePointer:
    {
        const mwinEvent* moved = Find(program, mwin_eventCursorMoved, 0);
        CHECK(program->entered && moved->data.pointer.position.x == 100.0f &&
                  moved->data.pointer.position.y == 120.0f,
              "the pointer enters and moves in the window's units");
        for (int i = 0; i < 2; i++)
        {
            Mouse(MOUSEEVENTF_LEFTDOWN, 0, 0, 0);
            Mouse(MOUSEEVENTF_LEFTUP, 0, 0, 0);
        }
        break;
    }
    case phaseClicks:
        CHECK(Find(program, mwin_eventButtonDown, 0)->data.pointer.clicks == 1 &&
                  Find(program, mwin_eventButtonDown, 1)->data.pointer.clicks == 2 &&
                  Find(program, mwin_eventButtonDown, 0)->data.pointer.button == mwin_buttonLeft &&
                  Find(program, mwin_eventButtonUp, 1)->data.pointer.buttons == 0,
              "a quick second click is a double click");
        Mouse(MOUSEEVENTF_WHEEL, 0, 0, WHEEL_DELTA);
        break;
    case phaseWheel:
        CHECK(Find(program, mwin_eventWheel, 0)->data.wheel.y == 1.0f,
              "a detent away from the user");
        break;
    default:
        break;
    }
}

// The cursor's phases.
static void AdvanceCursor(Program* program, mwinContext* context)
{
    mwinOutcome outcome = Outcome(program);
    switch (program->phase)
    {
    case phaseWheel:
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a text cursor");
        break;
    case phaseShape:
    {
        CHECK(outcome == mwin_outcomeDone && GetCursor() == LoadCursorA(nullptr, IDC_IBEAM),
              "the system's text cursor over the window");
        static uint8_t small[32 * 32 * 4];
        static uint8_t large[64 * 64 * 4];
        memset(small, 0xFF, sizeof(small));
        memset(large, 0xFF, sizeof(large));
        mwinIconImage images[2] = {{32, 32, 32 * 4, small}, {64, 64, 64 * 4, large}};
        mwinCursorDef def = mwinDefaultCursorDef();
        def.images = images;
        def.imageCount = 2;
        def.hotspotX = 4;
        def.hotspotY = 4;
        CHECK(mwinCreateCursor(context, &def, &program->cursor) == mwin_success &&
                  mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                      mwin_success,
              "a cursor made from images");
        break;
    }
    case phaseImage:
    case phaseImageAgain:
    {
        HCURSOR shown = GetCursor();
        CHECK(outcome == mwin_outcomeDone && shown != nullptr &&
                  shown != LoadCursorA(nullptr, IDC_IBEAM) &&
                  shown != LoadCursorA(nullptr, IDC_ARROW),
              "the cursor made from images over the window");
        if (program->phase == phaseImage)
        {
            CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                      mwin_success,
                  "a shape again");
            break;
        }
        CHECK(mwinDestroyCursor(context, program->cursor) == mwin_success &&
                  GetCursor() == LoadCursorA(nullptr, IDC_ARROW),
              "destroyed: the default shape");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                      mwin_success &&
                  mwinRequestCursorMode(context, program->window, mwin_cursorConfinedHidden,
                                        nullptr) == mwin_success,
              "the text cursor again; confine and hide");
        break;
    }
    case phaseShapeAgain:
        CHECK(outcome == mwin_outcomeDone && GetCursor() == LoadCursorA(nullptr, IDC_IBEAM),
              "the shape in the image's place");
        CHECK(mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                  mwin_success,
              "the images again");
        break;
    case phaseConfine:
        CHECK(outcome == mwin_outcomeDone && GetCursor() == nullptr &&
                  ClipIs(ClientOnScreen(program->hwnd)),
              "confined: hidden and clipped to the client area");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorVisible, nullptr) ==
                  mwin_success,
              "release");
        break;
    case phaseRelease:
        CHECK(outcome == mwin_outcomeDone && ClipIs(Desktop()) &&
                  GetCursor() == LoadCursorA(nullptr, IDC_IBEAM),
              "released: shown and free");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorCaptured, nullptr) ==
                  mwin_success,
              "capture");
        break;
    case phaseCapture:
    {
        RECT clip;
        CHECK(outcome == mwin_outcomeDone && GetClipCursor(&clip) && clip.right - clip.left == 1 &&
                  clip.bottom - clip.top == 1,
              "captured: held at one point");
        Mouse(MOUSEEVENTF_MOVE, 5, -3, 0);
        break;
    }
    case phaseRaw:
    {
        const mwinEvent* delta = Find(program, mwin_eventRawPointerDelta, 0);
        CHECK(delta->data.delta.x == 5.0f && delta->data.delta.y == -3.0f,
              "raw motion while captured");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorVisible, nullptr) ==
                  mwin_success,
              "release the capture");
        break;
    }
    case phaseUncapture:
        CHECK(outcome == mwin_outcomeDone && ClipIs(Desktop()), "released: free again");
        break;
    default:
        break;
    }
}

// The keyboard's phases.
static void AdvanceKeys(Program* program, mwinContext* context)
{
    const mwinEvent* down = Find(program, mwin_eventKeyDown, 0);
    switch (program->phase)
    {
    case phaseCreate:
        CHECK(mwinRequestFocus(context, program->window, nullptr) == mwin_success, "focus");
        break;
    case phaseFocus:
        Key(KEY_A, true);
        Key(KEY_A, false);
        break;
    case phaseKeys:
        CHECK(down != nullptr && down->data.key.code == mwin_codeKeyA &&
                  down->data.key.key == 'a' && !down->data.key.repeat && TextIs(program, "a"),
              "a key, what it means, and its text");
        CHECK(mwinMapKeyCode(context, mwin_codeKeyA) == 'a' &&
                  mwinMapKeyCode(context, mwin_codeEscape) == (MWIN_KEY_NAMED | mwin_codeEscape),
              "what keys mean in the layout");
        Key(KEY_SHIFT, true);
        Key(KEY_A, true);
        Key(KEY_A, false);
        Key(KEY_SHIFT, false);
        break;
    case phaseShift:
    {
        const mwinEvent* a = Find(program, mwin_eventKeyDown, 1);
        CHECK(a != nullptr && a->data.key.code == mwin_codeKeyA && a->data.key.key == 'a' &&
                  (a->data.key.modifiers & mwin_modShift) != 0 && TextIs(program, "A"),
              "Shift changes the text, not the meaning");
        // Windows drops a second injected press of a held key, so the
        // keyboard's repeat is posted as it would come: bit 30 set.
        PostMessageW(program->hwnd, WM_KEYDOWN, 'A', 0x1E0001);
        PostMessageW(program->hwnd, WM_KEYDOWN, 'A', 0x401E0001);
        PostMessageW(program->hwnd, WM_KEYUP, 'A', (LPARAM)0xC01E0001u);
        break;
    }
    case phaseRepeat:
    {
        const mwinEvent* second = Find(program, mwin_eventKeyDown, 1);
        CHECK(second != nullptr && !down->data.key.repeat, "two presses, the first no repeat");
        CHECK(second != nullptr && second->data.key.repeat, "the second press a repeat");
        CHECK(TextIs(program, "aa"), "a repeat types again");
        // U+1F600 in two UTF-16 units.
        Unit(0xD83D);
        Unit(0xDE00);
        break;
    }
    case phaseText:
    {
        RECT client = ClientOnScreen(program->hwnd);
        float scale = (float)GetDpiForWindow(program->hwnd) / (float)USER_DEFAULT_SCREEN_DPI;
        CHECK(TextIs(program, "\xF0\x9F\x98\x80") && Find(program, mwin_eventKeyDown, 0) == nullptr,
              "text outside the BMP, and no key for it");
        SetCursorPos(client.left + (int)(100.0f * scale), client.top + (int)(120.0f * scale));
        break;
    }
    default:
        break;
    }
}

// What a phase brought, for a failure's report.
static void Dump(const Program* program)
{
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        (void)printf("  phase %d: type %d, key %u, %s\n", (int)program->phase, (int)event->type,
                     (unsigned)event->data.key.code, event->data.key.repeat ? "repeat" : "");
    }
}

static void Advance(Program* program, mwinContext* context)
{
    int failures = s_failures;
    if (program->phase == phaseCreate)
    {
        mwinNativeHandles handles;
        CHECK(mwinGetNativeHandles(context, program->window, &handles) == mwin_success,
              "the window's HWND");
        program->hwnd = handles.handles.win32.hwnd;
    }
    if (program->phase <= phaseText)
    {
        AdvanceKeys(program, context);
    }
    else if (program->phase <= phaseClicks)
    {
        AdvancePointer(program);
    }
    else
    {
        AdvancePointer(program);
        AdvanceCursor(program, context);
    }
    if (s_failures != failures)
    {
        Dump(program);
    }
    program->phase += 1;
    program->count = 0;
    program->textLength = 0;
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
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        Sleep(1);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    Program program = {0};
    // The pointer starts outside the window.
    SetCursorPos(0, 0);
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on Windows");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    if (program.timedOut)
    {
        (void)printf("timed out in phase %d\n", (int)program.phase);
    }
    return s_failures == 0 ? 0 : 1;
}
