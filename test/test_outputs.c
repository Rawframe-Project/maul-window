// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a failed call on the context leaves in its outputs, on the test
// backend: every output filled with a pattern first, then each call made
// to fail with no context, with an id whose object is gone, from inside
// the context's allocator where it changes the context, and with a
// buffer too short where it copies out text or bytes. Every byte it
// leaves is zero, but for what the failure defines: the length a short
// buffer needs, and the dialog's count of paths. A call clears its
// outputs before any check, so these failures stand for the others each
// call has (a refused argument, a full limit).

#include "test_program.h"

#include "maul-window/accessibility.h"
#include "maul-window/clipboard.h"
#include "maul-window/dialog.h"
#include "maul-window/drop.h"
#include "maul-window/gamepad.h"
#include "maul-window/native.h"
#include "maul-window/services.h"
#include "maul-window/system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define ROOT "C:"
#else
#define ROOT ""
#endif

static const char s_chosen[] = ROOT "/a.png\0" ROOT "/b.png";
static const uint8_t s_pixels[16 * 16 * 4] = {0};
static const mwinIconImage s_image = {16, 16, 16 * 4, s_pixels};
static const mwinClipboardItem s_item = {"image/png", 9, "png", 3};
static const mwinHitRegion s_region = {{0.0f, 0.0f, 10.0f, 10.0f}, mwin_hitCaption};
static int s_root;

// The failure being driven, for a failed check's message.
static const char* s_kind;

// Whether a call answered expected and left every byte of its output
// zero.
static void Expect(const char* call, mwinResult status, mwinResult expected, const void* out,
                   size_t size)
{
    if (status != expected || !Zeroed(out, size))
    {
        printf("FAIL: %s %s: answered %d, %s\n", call, s_kind, (int)status,
               Zeroed(out, size) ? "its outputs zero" : "its outputs left");
        s_failures += 1;
    }
}

// Fills out with the pattern, makes the call and checks what it left.
#define EXPECT(expected, out, function, ...)                                                       \
    do                                                                                             \
    {                                                                                              \
        Fill(&(out), sizeof(out));                                                                 \
        Expect(#function, function(__VA_ARGS__), expected, &(out), sizeof(out));                   \
    } while (0)

// Two to four outputs a call writes, side by side with no padding, so
// that one fill and one check cover them all.
typedef struct Created
{
    mwinWindowId window;
    mwinRequestId request;
} Created;

typedef struct Rumble
{
    float low;
    float high;
    uint32_t durationMs;
    uint32_t count;
} Rumble;

typedef struct Shown
{
    mwinCursorId cursor;
    uint32_t image;
} Shown;

static_assert(sizeof(Created) == 4 * sizeof(uint32_t) && sizeof(Rumble) == 16 &&
                  sizeof(Shown) == 3 * sizeof(uint32_t),
              "no padding the calls would not clear");

// Every request of a window, with arguments it takes, each expected to
// answer status.
static void Requests(mwinContext* context, mwinWindowId window, mwinResult status)
{
    mwinRequestId id;
    mwinFileDialogDef dialog = mwinDefaultFileDialogDef();
    EXPECT(status, id, mwinWindow_RequestAccessibilityRoot, context, window, &s_root, &id);
    EXPECT(status, id, mwinWindow_RequestClipboardWrite, context, window, "Maul", 4, &id);
    EXPECT(status, id, mwinWindow_RequestClipboardRead, context, window, &id);
    EXPECT(status, id, mwinWindow_RequestClipboardWriteData, context, window, &s_item, 1, &id);
    EXPECT(status, id, mwinWindow_RequestClipboardReadData, context, window, "image/png", 9, &id);
    EXPECT(status, id, mwinWindow_RequestPrimaryWrite, context, window, "Maul", 4, &id);
    EXPECT(status, id, mwinWindow_RequestPrimaryRead, context, window, &id);
    EXPECT(status, id, mwinWindow_RequestFileDialog, context, window, &dialog, &id);
    EXPECT(status, id, mwinWindow_RequestVirtualKeyboard, context, window, true, mwin_purposeText,
           &id);
    EXPECT(status, id, mwinWindow_RequestTextInput, context, window, true,
           (mwinRect){0.0f, 0.0f, 1.0f, 1.0f}, &id);
    EXPECT(status, id, mwinWindow_RequestCursorMode, context, window, mwin_cursorVisible, &id);
    EXPECT(status, id, mwinWindow_RequestCursorShape, context, window, mwin_shapeDefault, &id);
    EXPECT(status, id, mwinWindow_RequestCursorImage, context, window, (mwinCursorId){0}, &id);
    EXPECT(status, id, mwinWindow_RequestOpenUrl, context, window, "https://a", 9, &id);
    EXPECT(status, id, mwinWindow_RequestRevealFile, context, window, ROOT "/a",
           sizeof(ROOT "/a") - 1, &id);
    EXPECT(status, id, mwinWindow_RequestKeepAwake, context, window, true, &id);
    EXPECT(status, id, mwinWindow_RequestTitle, context, window, "Maul", 4, &id);
    EXPECT(status, id, mwinWindow_RequestSize, context, window, (mwinSize){64.0f, 48.0f}, &id);
    EXPECT(status, id, mwinWindow_RequestPosition, context, window, (mwinPosition){0}, &id);
    EXPECT(status, id, mwinWindow_RequestMode, context, window, mwin_modeWindowed, &id);
    EXPECT(status, id, mwinWindow_RequestVisible, context, window, true, &id);
    EXPECT(status, id, mwinWindow_RequestSizeLimits, context, window, (mwinSize){1.0f, 1.0f},
           (mwinSize){100.0f, 100.0f}, &id);
    EXPECT(status, id, mwinWindow_RequestAspectRatio, context, window, 4, 3, &id);
    EXPECT(status, id, mwinWindow_RequestStyle, context, window, mwin_styleDecorated, &id);
    EXPECT(status, id, mwinWindow_RequestOpacity, context, window, 1.0f, &id);
    EXPECT(status, id, mwinWindow_RequestHitRegions, context, window, &s_region, 1, &id);
    EXPECT(status, id, mwinWindow_RequestIcon, context, window, &s_image, 1, &id);
    EXPECT(status, id, mwinWindow_RequestFocus, context, window, &id);
}

// The calls that change the context, each expected to answer status:
// the requests, the creations and the drain.
static void Changes(mwinContext* context, mwinWindowId window, mwinResult status)
{
    Requests(context, window, status);
    mwinWindowDef def = mwinDefaultWindowDef();
    Created created;
    EXPECT(status, created, mwinCreateWindow, context, &def, &created.window, &created.request);
    mwinCursorDef cursorDef = mwinDefaultCursorDef();
    cursorDef.images = &s_image;
    cursorDef.imageCount = 1;
    mwinCursorId cursor;
    EXPECT(status, cursor, mwinCreateCursor, context, &cursorDef, &cursor);
    mwinEvent event;
    EXPECT(status, event, mwinContext_NextEvent, context, &event);
    mwinMonitorInfo monitorInfo = {.scale = 1.0f};
    mwinMonitorId monitor;
    EXPECT(status, monitor, mwinAddTestMonitor, context, &monitorInfo, &monitor);
#ifdef MWIN_TEST_GAMEPAD
    mwinGamepadInfo padInfo = {.mapped = true, .battery = -1};
    mwinGamepadId pad;
    EXPECT(status, pad, mwinAddTestGamepad, context, &padInfo, &pad);
    mwinGamepadMotion motion;
    EXPECT(status, motion, mwinGamepad_GetMotion, context, (mwinGamepadId){0}, &motion);
#endif
}

// The calls on a gamepad, each expected to answer status.
static void Gamepad(mwinContext* context, mwinGamepadId gamepad, mwinResult status)
{
#ifdef MWIN_TEST_GAMEPAD
    mwinGamepadInfo info;
    EXPECT(status, info, mwinGamepad_GetInfo, context, gamepad, &info);
    mwinGamepadState state;
    EXPECT(status, state, mwinGamepad_GetState, context, gamepad, &state);
    bool enabled;
    EXPECT(status, enabled, mwinGamepad_IsMotionEnabled, context, gamepad, &enabled);
    mwinGamepadMotion motion;
    EXPECT(status, motion, mwinGamepad_GetMotion, context, gamepad, &motion);
    Rumble rumble;
    EXPECT(status, rumble, mwinGamepad_GetTestRumble, context, gamepad, &rumble.low, &rumble.high,
           &rumble.durationMs, &rumble.count);
    EXPECT(status, rumble, mwinGamepad_GetTestTriggerRumble, context, gamepad, &rumble.low,
           &rumble.high, &rumble.durationMs, &rumble.count);
    EXPECT(status, enabled, mwinGamepad_IsTestMotionEnabled, context, gamepad, &enabled);
#else
    (void)context;
    (void)gamepad;
    (void)status;
#endif
}

// The calls that only read a window or a monitor, each expected to
// answer status.
static void Reads(mwinContext* context, mwinWindowId window, mwinMonitorId monitor,
                  mwinResult status)
{
    mwinWindowState state;
    EXPECT(status, state, mwinWindow_GetState, context, window, &state);
    mwinNativeHandles handles;
    EXPECT(status, handles, mwinWindow_GetNativeHandles, context, window, &handles);
    Shown shown;
    EXPECT(status, shown, mwinWindow_GetTestCursor, context, window, &shown.cursor, &shown.image);
    mwinMonitorInfo info;
    EXPECT(status, info, mwinMonitor_GetInfo, context, monitor, &info);
    bool on;
    mwinRect caret;
    Fill(&on, sizeof(on));
    Fill(&caret, sizeof(caret));
    mwinResult text = mwinWindow_GetTestTextInput(context, window, &on, &caret);
    Expect("mwinWindow_GetTestTextInput", text, status, &on, sizeof(on));
    Expect("mwinWindow_GetTestTextInput", text, status, &caret, sizeof(caret));
    mwinInputPurpose purpose;
    Fill(&on, sizeof(on));
    Fill(&purpose, sizeof(purpose));
    mwinResult keyboard = mwinWindow_GetTestVirtualKeyboard(context, window, &on, &purpose);
    Expect("mwinWindow_GetTestVirtualKeyboard", keyboard, status, &on, sizeof(on));
    Expect("mwinWindow_GetTestVirtualKeyboard", keyboard, status, &purpose, sizeof(purpose));
}

// The calls that copy text or bytes out, each given a buffer and its
// length filled with the pattern, and expected to answer status and
// leave both zero.
static void Copies(mwinContext* context, mwinWindowId window, mwinRequestId request, uint32_t drop,
                   mwinResult status)
{
    struct
    {
        size_t length;
        char bytes[8];
    } buffer;
    static_assert(sizeof(buffer) == sizeof(size_t) + 8, "no padding");
    EXPECT(status, buffer, mwinRequest_GetClipboardText, context, request, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(status, buffer, mwinRequest_GetClipboardData, context, request, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(status, buffer, mwinRequest_GetPrimaryText, context, request, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(status, buffer, mwinContext_GetDroppedFiles, context, drop, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(status, buffer, mwinContext_GetDroppedText, context, drop, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(status, buffer, mwinWindow_GetTestTitle, context, window, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    struct
    {
        size_t length;
        uint32_t count;
        char bytes[4];
    } paths;
    static_assert(sizeof(paths) == sizeof(size_t) + 8, "no padding");
    EXPECT(status, paths, mwinRequest_GetDialogFiles, context, request, paths.bytes,
           sizeof(paths.bytes), &paths.length, &paths.count);
}

// The calls that read the context alone, given no context.
static void Context(void)
{
    struct
    {
        size_t length;
        char bytes[8];
    } buffer;
    EXPECT(mwin_errorInvalid, buffer, mwinContext_GetKeyboardLayout, nullptr, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(mwin_errorInvalid, buffer, mwinContext_GetPreferredLocales, nullptr, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(mwin_errorInvalid, buffer, mwinContext_GetTestClipboard, nullptr, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(mwin_errorInvalid, buffer, mwinContext_GetTestClipboardData, nullptr, "image/png", 9,
           buffer.bytes, sizeof(buffer.bytes), &buffer.length);
    EXPECT(mwin_errorInvalid, buffer, mwinContext_GetTestPrimary, nullptr, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
    EXPECT(mwin_errorInvalid, buffer, mwinContext_GetTestOpened, nullptr, mwin_requestOpenUrl,
           buffer.bytes, sizeof(buffer.bytes), &buffer.length);
    EXPECT(mwin_errorInvalid, buffer, mwinContext_GetTestDialog, nullptr, buffer.bytes,
           sizeof(buffer.bytes), &buffer.length);
#ifdef MWIN_TEST_GAMEPAD
    struct
    {
        size_t count;
        mwinGamepadId ids[2];
    } pads;
    EXPECT(mwin_errorInvalid, pads, mwinContext_GetGamepads, nullptr, pads.ids, 2, &pads.count);
#endif
    struct
    {
        size_t count;
        mwinMonitorId ids[2];
    } monitors;
    EXPECT(mwin_errorInvalid, monitors, mwinContext_GetMonitors, nullptr, monitors.ids, 2,
           &monitors.count);
    mwinKeyReach reach;
    EXPECT(mwin_errorInvalid, reach, mwinContext_GetKeyReach, nullptr, mwin_codeKeyA, 0, &reach);
    mwinSystemFacts facts;
    EXPECT(mwin_errorInvalid, facts, mwinContext_GetSystemFacts, nullptr, &facts);
    uint32_t count;
    uint64_t checksum;
    Fill(&count, sizeof(count));
    Fill(&checksum, sizeof(checksum));
    mwinResult status = mwinContext_GetTestIcon(nullptr, &count, &checksum);
    Expect("mwinContext_GetTestIcon", status, mwin_errorInvalid, &count, sizeof(count));
    Expect("mwinContext_GetTestIcon", status, mwin_errorInvalid, &checksum, sizeof(checksum));
}

// A buffer one byte short of what a call has, filled with the pattern:
// it must answer mwin_errorCapacity with the length it needs and leave
// the buffer zero.
static void Short(const char* call, mwinResult status, const char* buffer, size_t capacity,
                  size_t length, size_t needed)
{
    if (status != mwin_errorCapacity || length != needed || !Zeroed(buffer, capacity))
    {
        printf("FAIL: %s with a buffer too short: answered %d, length %zu of %zu, %s\n", call,
               (int)status, length, needed,
               Zeroed(buffer, capacity) ? "the buffer zero" : "the buffer left");
        s_failures += 1;
    }
}

#define SHORT(needed, function, ...)                                                               \
    do                                                                                             \
    {                                                                                              \
        char bytes[(needed) - 1];                                                                  \
        size_t length = 0xA5A5;                                                                    \
        Fill(bytes, sizeof(bytes));                                                                \
        mwinResult status = function(__VA_ARGS__, bytes, sizeof(bytes), &length);                  \
        Short(#function, status, bytes, sizeof(bytes), length, needed);                            \
    } while (0)

static void Shorts(Program* program, mwinContext* context, uint32_t drop)
{
    SHORT(9, mwinRequest_GetClipboardText, context, program->requests[1]);
    SHORT(3, mwinRequest_GetClipboardData, context, program->requests[2]);
    SHORT(7, mwinRequest_GetPrimaryText, context, program->requests[3]);
    SHORT(4, mwinContext_GetDroppedFiles, context, drop);
    SHORT(4, mwinContext_GetDroppedText, context, drop);
    SHORT(12, mwinContext_GetKeyboardLayout, context);
    SHORT(11, mwinContext_GetPreferredLocales, context);
    SHORT(4, mwinWindow_GetTestTitle, context, program->windows[0]);
    SHORT(9, mwinContext_GetTestClipboard, context);
    SHORT(7, mwinContext_GetTestPrimary, context);
    SHORT(9, mwinContext_GetTestOpened, context, mwin_requestOpenUrl);
    char dialog[4];
    size_t length = 0;
    Fill(dialog, sizeof(dialog));
    mwinResult status = mwinContext_GetTestDialog(context, dialog, sizeof(dialog), &length);
    CHECK(status == mwin_errorCapacity && length > sizeof(dialog) && Zeroed(dialog, sizeof(dialog)),
          "the dialog the test platform was asked for, zero in a buffer too short");
    char paths[4];
    uint32_t count = 0;
    Fill(paths, sizeof(paths));
    status = mwinRequest_GetDialogFiles(context, program->requests[4], paths, sizeof(paths),
                                        &length, &count);
    CHECK(status == mwin_errorCapacity && length == sizeof(s_chosen) && count == 2 &&
              Zeroed(paths, sizeof(paths)),
          "a dialog's paths: their bytes and count kept, none of them in a buffer too short");
}

// The calls the allocator makes when armed, and how often it did.
static mwinContext* s_context;
static mwinWindowId s_window;
static bool s_armed;
static int s_entered;

static void* ArmedAllocate(size_t size, size_t alignment, void* user)
{
    (void)user;
    if (s_armed)
    {
        s_entered += 1;
        s_kind = "from inside the allocator";
        Changes(s_context, s_window, mwin_errorBusy);
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

static void ArmedFree(void* memory, size_t size, size_t alignment, void* user)
{
    (void)size;
    (void)alignment;
    (void)user;
    free(memory);
}

// What a test asks of the test platform before the failures: the reads,
// a dialog and an address answered, and a drop. The test clipboard holds
// one kind at a time, so its text comes after the data was read.
static void Ask(Program* program, mwinContext* context)
{
    mwinWindowId window = program->windows[0];
    mwinFileDialogDef dialog = mwinDefaultFileDialogDef();
    CHECK(mwinContext_SetTestClipboardData(context, "image/png", 9, "png", 3) == mwin_success &&
              mwinContext_SetTestPrimary(context, "Primary", 7) == mwin_success &&
              mwinContext_SetTestDialogFiles(context, s_chosen, sizeof(s_chosen)) == mwin_success &&
              mwinContext_SetTestLocales(context, "en-US,tr-TR", 11) == mwin_success,
          "the test platform's clipboard data, primary, dialog paths and locales");
    CHECK(mwinWindow_RequestClipboardReadData(context, window, "image/png", 9,
                                              &program->requests[2]) == mwin_success &&
              mwinWindow_RequestPrimaryRead(context, window, &program->requests[3]) ==
                  mwin_success &&
              mwinWindow_RequestFileDialog(context, window, &dialog, &program->requests[4]) ==
                  mwin_success &&
              mwinWindow_RequestOpenUrl(context, window, "https://a", 9, nullptr) == mwin_success &&
              mwinWindow_PostTestDrop(context, window, (mwinPosition){0}, "/ab\0", 4, "drop", 4) ==
                  mwin_success,
          "the reads, a dialog, an address and a drop");
}

// The drop the test posted, by its number.
static uint32_t s_drop;

static uint32_t Dropped(const Program* program)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        if (program->events[i].type == mwin_eventDropped)
        {
            return program->events[i].data.drop.drop;
        }
    }
    return 0;
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    switch (step)
    {
    case 0:
    {
        program->windows[0] = Create(context, &program->requests[0]);
        program->windows[1] = Create(context, nullptr);
        mwinMonitorInfo info = {.scale = 1.0f};
        mwinMonitorId monitor = {0};
        CHECK(mwinDestroyWindow(context, program->windows[1]) == mwin_success &&
                  mwinAddTestMonitor(context, &info, &monitor) == mwin_success &&
                  mwinRemoveTestMonitor(context, monitor) == mwin_success,
              "a window and a monitor gone");
        s_kind = "with no context";
        Changes(nullptr, program->windows[0], mwin_errorInvalid);
        Gamepad(nullptr, (mwinGamepadId){0}, mwin_errorInvalid);
        Reads(nullptr, program->windows[0], monitor, mwin_errorInvalid);
        Copies(nullptr, program->windows[0], (mwinRequestId){0}, 0, mwin_errorInvalid);
        Context();
        s_kind = "with an id whose object is gone";
        Requests(context, program->windows[1], mwin_errorStale);
        Reads(context, program->windows[1], monitor, mwin_errorStale);
        Copies(context, program->windows[1], program->requests[0], 1, mwin_errorStale);
#ifdef MWIN_TEST_GAMEPAD
        mwinGamepadInfo padInfo = {.mapped = true, .battery = -1};
        mwinGamepadId gamepad = {0};
        CHECK(mwinAddTestGamepad(context, &padInfo, &gamepad) == mwin_success &&
                  mwinRemoveTestGamepad(context, gamepad) == mwin_success,
              "a gamepad gone");
        Gamepad(context, gamepad, mwin_errorStale);
#endif
        Ask(program, context);
        break;
    }
    case 1:
        s_drop = Dropped(program);
        CHECK(s_drop != 0, "the drop delivered");
        SHORT(3, mwinContext_GetTestClipboardData, context, "image/png", 9);
        CHECK(mwinContext_SetTestClipboard(context, "Maul text", 9) == mwin_success &&
                  mwinWindow_RequestClipboardRead(context, program->windows[0],
                                                  &program->requests[1]) == mwin_success,
              "the test platform's clipboard text, read");
        break;
    case 2:
    {
        Shorts(program, context, s_drop);
        s_context = context;
        s_window = program->windows[0];
        s_armed = true;
        mwinResult status = mwinWindow_RequestClipboardWrite(context, s_window, "Maul", 4, nullptr);
        s_armed = false;
        CHECK(status == mwin_success && s_entered > 0,
              "a clipboard write that calls in from its allocator");
        break;
    }
    default:
        program->done = true;
        break;
    }
}

int main(void)
{
    Program program = {.step = Step};
    mwinContextDef def = mwinDefaultContextDef();
    def.allocator = (mwinAllocator){ArmedAllocate, ArmedFree, nullptr};
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
