// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Android's input through the activity's input queue, in the emulator
// (tools/run_android_app.sh): what the system's own input command
// injects, which the test asks the runner for, after a frame shown, as
// Android 14 and later send no touch to a window whose surface never
// showed one. A tap and a swipe on the touch screen as one touch each,
// in logical units; a mouse's tap as the left button's click; a
// stylus's tap as the pen touching (where the command marks it a
// stylus); the A key as its key, its meaning and its text; typed text
// through the virtual keyboard's keys, Shift's press named; Escape
// taken by the program, so that the activity stays, and Menu, which
// Android counts a system key, left to it; and, from Android 13, whose
// command can scroll, a mouse's wheel. quit writes the closing line the
// runner waits for.

#include "test_harness.h"

#include "maul-window/context.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <android/api-level.h>
#include <android/native_window.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 20000000000ull
#define OUT_PATH    "/data/data/" MWIN_TEST_PACKAGE "/files/out"
#define RECORDS     128
#define LAST_PHASE  8

typedef struct Program
{
    int phase;
    uint64_t startNs;
    mwinWindowId window;
    bool shown;
    // The input records since the phase began, and the text typed.
    mwinEvent records[RECORDS];
    int recordCount;
    char text[64];
    size_t textLength;
    // The window's middle in pixels, and its scale.
    int x;
    int y;
    float scale;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static bool IsInput(mwinEventType type)
{
    return (type >= mwin_eventKeyDown && type <= mwin_eventPenButtonUp);
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown |= event.type == mwin_eventShown;
        if (event.type == mwin_eventTextInput &&
            program->textLength + event.data.text.length < sizeof(program->text))
        {
            memcpy(program->text + program->textLength, event.data.text.text,
                   event.data.text.length);
            program->textLength += event.data.text.length;
        }
        if (IsInput(event.type) && program->recordCount < RECORDS)
        {
            program->records[program->recordCount++] = event;
        }
    }
}

// The first record of a type since the phase began, or null.
static const mwinEvent* Find(const Program* program, mwinEventType type)
{
    for (int i = 0; i < program->recordCount; i++)
    {
        if (program->records[i].type == type)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

static int Count(const Program* program, mwinEventType type)
{
    int count = 0;
    for (int i = 0; i < program->recordCount; i++)
    {
        count += program->records[i].type == type;
    }
    return count;
}

static const mwinEvent* KeyUpOf(const Program* program, mwinKeyCode code)
{
    for (int i = 0; i < program->recordCount; i++)
    {
        if (program->records[i].type == mwin_eventKeyUp &&
            program->records[i].data.key.code == code)
        {
            return &program->records[i];
        }
    }
    return nullptr;
}

static bool Near(mwinPosition position, float x, float y)
{
    return fabsf(position.x - x) < 1.0f && fabsf(position.y - y) < 1.0f;
}

static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case 0:
        return program->shown;
    case 1:
    case 2:
        return Find(program, mwin_eventTouchUp) != nullptr;
    case 3:
        return Find(program, mwin_eventButtonUp) != nullptr;
    case 4:
        return Find(program, mwin_eventPenUp) != nullptr ||
               Find(program, mwin_eventTouchUp) != nullptr;
    case 5:
        return KeyUpOf(program, mwin_codeKeyA) != nullptr;
    case 6:
        return program->textLength >= 2;
    case 7:
        return KeyUpOf(program, mwin_codeKeyB) != nullptr;
    case 8:
        return android_get_device_api_level() < 33 || Find(program, mwin_eventWheel) != nullptr;
    default:
        return true;
    }
}

static void CheckTap(const Program* program)
{
    const mwinEvent* down = Find(program, mwin_eventTouchDown);
    const mwinEvent* up = Find(program, mwin_eventTouchUp);
    float x = (float)program->x / program->scale;
    float y = (float)program->y / program->scale;
    CHECK(down != nullptr && up != nullptr && down->data.touch.id == up->data.touch.id &&
              Near(down->data.touch.position, x, y) && Count(program, mwin_eventTouchDown) == 1,
          "a tap: one touch, where it was, in logical units");
    CHECK(Find(program, mwin_eventButtonDown) == nullptr &&
              Find(program, mwin_eventPenDown) == nullptr,
          "a finger is no button and no pen");
}

static void CheckSwipe(const Program* program)
{
    const mwinEvent* down = Find(program, mwin_eventTouchDown);
    const mwinEvent* up = Find(program, mwin_eventTouchUp);
    CHECK(down != nullptr && up != nullptr && Count(program, mwin_eventTouchMoved) >= 1 &&
              up->data.touch.position.x > down->data.touch.position.x + 20.0f,
          "a swipe: down, moves, up further right");
}

static void CheckClick(const Program* program)
{
    const mwinEvent* down = Find(program, mwin_eventButtonDown);
    CHECK(down != nullptr && down->data.pointer.button == mwin_buttonLeft &&
              down->data.pointer.clicks >= 1 && down->data.pointer.buttons == 1 &&
              Find(program, mwin_eventTouchDown) == nullptr,
          "a mouse's tap: the left button, not a touch");
}

// The input command marks its stylus as one on Android 15 (a finger on
// Android 11, which the touch screen tells as a touch); the versions
// between were not seen, and may do either.
static void CheckStylus(const Program* program)
{
    const mwinEvent* down = Find(program, mwin_eventPenDown);
    bool pen = down != nullptr && (down->data.pen.flags & mwin_penContact) != 0 &&
               Find(program, mwin_eventTouchDown) == nullptr;
    bool touch = down == nullptr && Find(program, mwin_eventTouchDown) != nullptr;
    int level = android_get_device_api_level();
    CHECK(level >= 35   ? pen
          : level <= 30 ? touch
                        : pen || touch,
          "a stylus's tap: the pen touching (a touch where the command sends a finger)");
}

static void CheckKey(const Program* program)
{
    const mwinEvent* down = Find(program, mwin_eventKeyDown);
    CHECK(down != nullptr && down->data.key.code == mwin_codeKeyA && down->data.key.key == 'a' &&
              !down->data.key.repeat,
          "the A key, meaning a");
    CHECK(program->textLength == 1 && program->text[0] == 'a', "typing a");
}

static void CheckTyped(const Program* program, mwinContext* context)
{
    CHECK(program->textLength == 2 && memcmp(program->text, "Hi", 2) == 0, "typed text");
    const mwinEvent* shift = Find(program, mwin_eventKeyDown);
    CHECK(shift != nullptr && shift->data.key.code == mwin_codeShiftLeft &&
              shift->data.key.key == (MWIN_KEY_NAMED | mwin_codeShiftLeft),
          "Shift's press, named");
    CHECK(mwinMapKeyCode(context, mwin_codeKeyH) == 'h', "H learned to mean h");
}

static void CheckEscape(const Program* program)
{
    const mwinEvent* escape = Find(program, mwin_eventKeyDown);
    CHECK(escape != nullptr && escape->data.key.code == mwin_codeEscape,
          "Escape taken by the program");
    CHECK(Count(program, mwin_eventKeyDown) == 2,
          "Menu, a system key with a code, left to Android");
}

// Shows a frame, drawn on the CPU: Android 14 and later send no touch to
// a window whose surface never showed one.
static void Paint(const Program* program, mwinContext* context)
{
    mwinNativeHandles handles = {0};
    if (mwinGetNativeHandles(context, program->window, &handles) != mwin_success)
    {
        return;
    }
    ANativeWindow* window = handles.handles.android.window;
    ANativeWindow_Buffer buffer;
    (void)ANativeWindow_setBuffersGeometry(window, 0, 0, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    if (ANativeWindow_lock(window, &buffer, nullptr) == 0)
    {
        for (int32_t row = 0; row < buffer.height; row++)
        {
            memset((uint8_t*)buffer.bits + (size_t)row * (size_t)buffer.stride * 4u, 0x40,
                   (size_t)buffer.width * 4u);
        }
        (void)ANativeWindow_unlockAndPost(window);
    }
}

static void Advance(Program* program, mwinContext* context)
{
    switch (program->phase)
    {
    case 0:
    {
        Paint(program, context);
        mwinWindowState state = {0};
        (void)mwinGetWindowState(context, program->window, &state);
        program->x = (int)state.pixelSize.width / 2;
        program->y = (int)state.pixelSize.height / 2;
        program->scale = state.scale;
        printf("adb: input tap %d %d\n", program->x, program->y);
        break;
    }
    case 1:
        CheckTap(program);
        printf("adb: input swipe %d %d %d %d 300\n", program->x, program->y, program->x + 200,
               program->y);
        break;
    case 2:
        CheckSwipe(program);
        printf("adb: input mouse tap %d %d\n", program->x, program->y);
        break;
    case 3:
        CheckClick(program);
        printf("adb: input stylus tap %d %d\n", program->x, program->y);
        break;
    case 4:
        CheckStylus(program);
        program->textLength = 0;
        printf("adb: input keyevent KEYCODE_A\n");
        break;
    case 5:
        CheckKey(program);
        program->textLength = 0;
        printf("adb: input text Hi\n");
        break;
    case 6:
        CheckTyped(program, context);
        printf("adb: input keyevent KEYCODE_ESCAPE\n");
        printf("adb: input keyevent KEYCODE_MENU\n");
        printf("adb: input keyevent KEYCODE_B\n");
        break;
    case 7:
        CheckEscape(program);
        if (android_get_device_api_level() >= 33)
        {
            printf("adb: input mouse scroll %d %d --axis VSCROLL,1\n", program->x, program->y);
        }
        break;
    case 8:
    {
        const mwinEvent* wheel = Find(program, mwin_eventWheel);
        CHECK(wheel == nullptr || wheel->data.wheel.y == 1.0f, "the wheel, a detent away");
        break;
    }
    default:
        break;
    }
    program->phase += 1;
    program->recordCount = 0;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    if (program->phase > LAST_PHASE)
    {
        return mwin_frameStop;
    }
    if (Ready(program))
    {
        printf("phase %d\n", program->phase);
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        printf("timed out in phase %d after %d records:", program->phase, program->recordCount);
        for (int i = 0; i < program->recordCount; i++)
        {
            printf(" %d", (int)program->records[i].type);
        }
        printf("\n");
        s_failures += 1;
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    CHECK(status == mwin_success, "init succeeded");
    CHECK(program->phase > LAST_PHASE, "every phase ran");
    printf("result: %d failures\n", s_failures);
}

mwinAppDef mwinAndroidMain(void)
{
    static Program program;
    // The runner reads the file with run-as.
    if (freopen(OUT_PATH, "w", stdout) != nullptr)
    {
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    return def;
}
