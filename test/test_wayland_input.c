// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend's input against the test compositor of
// wayland_server.h. The keyboard: keys with their codes, meanings and
// text, the modifiers, repeat, a compose sequence (a dead key), a change
// of layout group with its record and new meanings, and the reset when
// focus goes while a key is held. The pointer: entering, the last motion
// of a frame, quick clicks counted, high-resolution wheel steps counted
// once, and leaving; and a touch stroke. The cursor: a shape through
// the cursor shape protocol, a cursor made from images on a surface of
// shared memory sized by a viewport, the default shape once it is
// destroyed, hiding, capture as a locked pointer with raw relative
// motion, and release. The input method: enabling with the caret, a
// composition, its commit, and disabling. The frame the
// backend draws, as the compositor offers no server-side decorations:
// the window geometry with the caption, and the caption moving the
// window, its close button, a resize edge and a double click, none of
// which reaches the program as pointer input. Skipped (exit status 77)
// without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"
#include "maul-window/input.h"

#include <linux/input-event-codes.h>
#include <string.h>
#include <time.h>

#define DEADLINE_NS 5000000000ull
#define MAX_RECORDS 256
#define TEXT_BYTES  1024

typedef enum Phase
{
    phaseCreate,
    phaseKeys,
    phaseShift,
    phaseRepeat,
    phaseCompose,
    phaseLayout,
    phaseLeave,
    phasePointerEnter,
    phaseMotion,
    phaseClicks,
    phaseWheel,
    phaseTouch,
    phaseShape,
    phaseImage,
    phaseDestroyed,
    phaseHidden,
    phaseCaptured,
    phaseRaw,
    phaseVisible,
    phasePointerLeave,
    phaseTextEnable,
    phasePreedit,
    phaseTextCommit,
    phaseTextDisable,
    phaseFrame,
    phaseMove,
    phaseCloseButton,
    phaseResize,
    phaseDoubleClick,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    mwinCursorId cursor;
    // The records since the phase began; their text is copied.
    mwinEvent records[MAX_RECORDS];
    int count;
    char text[TEXT_BYTES];
    uint32_t textLength;
    bool timedOut;
    // The frame part the pointer is on.
    struct wl_resource* part;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success && program->count < MAX_RECORDS)
    {
        if (event.type == mwin_eventTextInput &&
            program->textLength + event.data.text.length <= TEXT_BYTES)
        {
            memcpy(program->text + program->textLength, event.data.text.text,
                   event.data.text.length);
            program->textLength += event.data.text.length;
        }
        program->records[program->count++] = event;
    }
}

static int CountOf(const Program* program, mwinEventType type, bool repeat)
{
    int count = 0;
    for (int i = 0; i < program->count; i++)
    {
        const mwinEvent* event = &program->records[i];
        count +=
            event->type == type && (type != mwin_eventKeyDown || event->data.key.repeat == repeat);
    }
    return count;
}

static const mwinEvent* First(const Program* program, mwinEventType type)
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

// The wheel's vertical detents in the records.
static float Turned(const Program* program)
{
    float turned = 0.0f;
    for (int i = 0; i < program->count; i++)
    {
        turned +=
            program->records[i].type == mwin_eventWheel ? program->records[i].data.wheel.y : 0.0f;
    }
    return turned;
}

static bool TextIs(const Program* program, const char* text)
{
    return program->textLength == strlen(text) && memcmp(program->text, text, strlen(text)) == 0;
}

// Whether the compositor has seen what the frame asked of it.
static bool FrameReady(Phase phase, Shell shell)
{
    switch (phase)
    {
    case phaseFrame:
        return shell.parts == 5 && shell.geometry[1] == -28;
    case phaseMove:
        return shell.moves > 0;
    case phaseResize:
        return shell.resizeEdge != 0;
    default:
        return shell.maximizes > 0;
    }
}

// Whether the compositor has seen what a cursor phase asked for.
static bool CursorReady(Phase phase, Cursor cursor)
{
    switch (phase)
    {
    case phaseShape:
        return cursor.shape == WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    case phaseImage:
        return cursor.images > 0;
    case phaseHidden:
        return cursor.hides > 0;
    case phaseCaptured:
        return cursor.locked;
    default:
        return !cursor.locked;
    }
}

// Whether what the phase waits for has come.
static bool Ready(const Program* program)
{
    switch (program->phase)
    {
    case phaseCreate:
        return First(program, mwin_eventRequestCompleted) != nullptr;
    case phaseKeys:
    case phaseShift:
    case phaseCompose:
        return CountOf(program, mwin_eventKeyUp, false) >= (program->phase == phaseCompose ? 2 : 1);
    case phaseRepeat:
        return CountOf(program, mwin_eventKeyDown, true) >= 2;
    case phaseLayout:
        return First(program, mwin_eventKeyboardLayoutChanged) != nullptr;
    case phaseLeave:
        return First(program, mwin_eventInputStateReset) != nullptr;
    case phasePointerEnter:
        return First(program, mwin_eventCursorEntered) != nullptr;
    case phaseMotion:
        return First(program, mwin_eventCursorMoved) != nullptr;
    case phaseClicks:
        return CountOf(program, mwin_eventButtonUp, false) >= 2;
    case phaseWheel:
        return Turned(program) <= -1.0f;
    case phaseTouch:
        return First(program, mwin_eventTouchUp) != nullptr;
    case phaseDestroyed:
        // No request: the destroyed cursor's window shows the default.
        return ServerCursor(program->server).shape == WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    case phaseShape:
    case phaseImage:
    case phaseHidden:
    case phaseCaptured:
    case phaseVisible:
        return First(program, mwin_eventRequestCompleted) != nullptr &&
               CursorReady(program->phase, ServerCursor(program->server));
    case phaseRaw:
        return First(program, mwin_eventRawPointerDelta) != nullptr;
    case phasePointerLeave:
        return First(program, mwin_eventCursorLeft) != nullptr;
    case phaseTextEnable:
    case phaseTextDisable:
    {
        TextState text = ServerText(program->server);
        return First(program, mwin_eventRequestCompleted) != nullptr &&
               text.enabled == (program->phase == phaseTextEnable);
    }
    case phasePreedit:
        return First(program, mwin_eventImePreedit) != nullptr;
    case phaseFrame:
    case phaseMove:
    case phaseResize:
    case phaseDoubleClick:
        return FrameReady(program->phase, ServerShell(program->server));
    case phaseCloseButton:
        return First(program, mwin_eventCloseRequested) != nullptr;
    case phaseTextCommit:
        return First(program, mwin_eventTextInput) != nullptr;
    default:
        return true;
    }
}

static void Press(Server* server, uint32_t evdev)
{
    ServerKey(server, evdev, true);
    ServerKey(server, evdev, false);
}

// Clicks a point of a frame part with a button.
static void ClickPart(Program* program, struct wl_resource* part, double x, double y,
                      uint32_t button)
{
    if (program->part != nullptr)
    {
        ServerPointerLeaveFrom(program->server, program->part);
    }
    program->part = part;
    ServerPointerEnterOn(program->server, part, x, y);
    ServerButton(program->server, button, true);
    ServerButton(program->server, button, false);
}

// The frame's phases.
static void AdvanceFrame(Program* program)
{
    Server* server = program->server;
    Shell shell = ServerShell(server);
    struct wl_resource* caption = ServerPartAt(server, 0, -28);
    switch (program->phase)
    {
    case phaseFrame:
        CHECK(shell.geometry[0] == 0 && shell.geometry[2] == 1280 && shell.geometry[3] == 748 &&
                  caption != nullptr,
              "the window geometry holds the caption above the content");
        ClickPart(program, caption, 100.0, 10.0, BTN_LEFT);
        break;
    case phaseMove:
        CHECK(First(program, mwin_eventCursorEntered) == nullptr &&
                  First(program, mwin_eventButtonDown) == nullptr,
              "the caption moves the window, and the program sees none of it");
        ClickPart(program, caption, 1270.0, 10.0, BTN_LEFT);
        break;
    case phaseCloseButton:
        // The left margin reaches from above the caption to below the
        // content.
        ClickPart(program, ServerPartAt(server, -6, -34), 3.0, 400.0, BTN_LEFT);
        break;
    case phaseResize:
        CHECK(shell.resizeEdge == XDG_TOPLEVEL_RESIZE_EDGE_LEFT, "the left margin resizes");
        ClickPart(program, caption, 300.0, 10.0, BTN_LEFT);
        ServerButton(server, BTN_LEFT, true);
        ServerButton(server, BTN_LEFT, false);
        break;
    case phaseDoubleClick:
        CHECK(shell.maximizes == 1, "a double click on the caption maximizes");
        ServerPointerLeaveFrom(server, program->part);
        break;
    default:
        break;
    }
}

// The input method's phases.
static void AdvanceText(Program* program, mwinContext* context)
{
    const mwinEvent* preedit = First(program, mwin_eventImePreedit);
    switch (program->phase)
    {
    case phasePointerLeave:
        ServerTextEnter(program->server);
        CHECK(mwinRequestTextInput(context, program->window, true,
                                   (mwinRect){10.0f, 20.0f, 1.0f, 16.0f}, nullptr) == mwin_success,
              "accept text");
        break;
    case phaseTextEnable:
    {
        TextState text = ServerText(program->server);
        CHECK(text.x == 10 && text.y == 20 && text.width == 1 && text.height == 16,
              "the input method knows where the caret is");
        // "kan", with the caret after it.
        ServerCompose(program->server, "\xE3\x81\x8B\xE3\x82\x93", 6, 6, nullptr);
        break;
    }
    case phasePreedit:
        CHECK(preedit->data.preedit.length == 6 && preedit->data.preedit.caret == 6 &&
                  preedit->data.preedit.segmentCount == 1 &&
                  preedit->data.preedit.segments[0].length == 6 &&
                  memcmp(preedit->data.preedit.text, "\xE3\x81\x8B\xE3\x82\x93", 6) == 0,
              "a composition, underlined, with its caret");
        ServerCompose(program->server, nullptr, 0, 0, "\xE6\xBC\xA2");
        break;
    case phaseTextDisable:
        break;
    case phaseTextCommit:
        CHECK(TextIs(program, "\xE6\xBC\xA2") && preedit != nullptr &&
                  preedit->data.preedit.length == 0 &&
                  First(program, mwin_eventTextInput) < preedit,
              "the commit, then the composition's end");
        CHECK(mwinRequestTextInput(context, program->window, false, (mwinRect){0}, nullptr) ==
                  mwin_success,
              "stop accepting text");
        break;
    default:
        AdvanceFrame(program);
        break;
    }
}

// A cursor of two images: red at 16 pixels, green at 32.
static uint8_t s_red[16 * 16 * 4];
static uint8_t s_green[32 * 32 * 4];

static mwinCursorDef CursorDef(mwinIconImage images[2])
{
    for (size_t i = 0; i < sizeof(s_green); i += 4)
    {
        memcpy(&s_green[i], (const uint8_t[]){0, 255, 0, 255}, 4);
        if (i < sizeof(s_red))
        {
            memcpy(&s_red[i], (const uint8_t[]){255, 0, 0, 255}, 4);
        }
    }
    images[0] = (mwinIconImage){16, 16, 16 * 4, s_red};
    images[1] = (mwinIconImage){32, 32, 32 * 4, s_green};
    mwinCursorDef def = mwinDefaultCursorDef();
    def.images = images;
    def.imageCount = 2;
    def.hotspotX = 3;
    def.hotspotY = 5;
    return def;
}

// The cursor's phases.
static void AdvanceCursor(Program* program, mwinContext* context)
{
    const mwinEvent* completed = First(program, mwin_eventRequestCompleted);
    bool done = completed != nullptr && completed->data.completion.outcome == mwin_outcomeDone;
    switch (program->phase)
    {
    case phaseShape:
    {
        CHECK(done, "the shape through the cursor shape protocol");
        mwinIconImage images[2];
        mwinCursorDef def = CursorDef(images);
        CHECK(mwinCreateCursor(context, &def, &program->cursor) == mwin_success &&
                  mwinRequestCursorImage(context, program->window, program->cursor, nullptr) ==
                      mwin_success,
              "a cursor made from images");
        break;
    }
    case phaseImage:
    {
        Cursor cursor = ServerCursor(program->server);
        CHECK(done && cursor.width == 16 && cursor.height == 16 && cursor.scale == 1 &&
                  cursor.destinationWidth == 16 && cursor.destinationHeight == 16 &&
                  cursor.hotspotX == 3 && cursor.hotspotY == 5 && cursor.pixel == 0xFFFF0000u,
              "the image for scale 1 in shared memory, its viewport and hotspot");
        CHECK(mwinDestroyCursor(context, program->cursor) == mwin_success, "destroyed");
        break;
    }
    case phaseDestroyed:
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorHidden, nullptr) ==
                  mwin_success,
              "hide");
        break;
    case phaseHidden:
        CHECK(done, "hidden with no cursor surface");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorCaptured, nullptr) ==
                  mwin_success,
              "capture");
        break;
    case phaseCaptured:
        CHECK(done, "captured as a locked pointer");
        ServerRelativeMotion(program->server, 3.0, -2.0);
        break;
    case phaseRaw:
    {
        const mwinEvent* delta = First(program, mwin_eventRawPointerDelta);
        CHECK(delta->data.delta.x == 3.0f && delta->data.delta.y == -2.0f,
              "raw deltas, before acceleration");
        CHECK(mwinRequestCursorMode(context, program->window, mwin_cursorVisible, nullptr) ==
                  mwin_success,
              "release");
        break;
    }
    case phaseVisible:
        CHECK(done, "released: the lock goes");
        ServerPointerLeave(program->server);
        break;
    default:
        AdvanceText(program, context);
        break;
    }
}

// The pointer's and the touch screen's phases.
static void AdvancePointer(Program* program, mwinContext* context)
{
    Server* server = program->server;
    const mwinEvent* entered = First(program, mwin_eventCursorEntered);
    const mwinEvent* moved = First(program, mwin_eventCursorMoved);
    switch (program->phase)
    {
    case phasePointerEnter:
        CHECK(entered->data.pointer.position.x == 10.0f &&
                  entered->data.pointer.position.y == 20.0f,
              "the pointer enters where it is");
        ServerPointerMotion(server, 30.5, 40.25);
        break;
    case phaseMotion:
        CHECK(CountOf(program, mwin_eventCursorMoved, false) == 1 &&
                  moved->data.pointer.position.x == 30.5f &&
                  moved->data.pointer.position.y == 40.25f,
              "one motion per frame, the last");
        for (int i = 0; i < 2; i++)
        {
            ServerButton(server, BTN_LEFT, true);
            ServerButton(server, BTN_LEFT, false);
        }
        break;
    case phaseClicks:
    {
        const mwinEvent* second = nullptr;
        for (int i = 0; i < program->count; i++)
        {
            second =
                program->records[i].type == mwin_eventButtonDown ? &program->records[i] : second;
        }
        const mwinEvent* first = First(program, mwin_eventButtonDown);
        CHECK(first != nullptr && first->data.pointer.button == mwin_buttonLeft &&
                  first->data.pointer.clicks == 1 && first->data.pointer.buttons == 1 &&
                  second != first && second->data.pointer.clicks == 2,
              "a quick second click is a double click");
        ServerWheel(server, 60);
        ServerWheel(server, 60);
        break;
    }
    case phaseWheel:
        CHECK(Turned(program) == -1.0f,
              "two half steps toward the user make one detent, counted once");
        ServerTouchStroke(server, 7);
        break;
    case phaseTouch:
    {
        const mwinEvent* down = First(program, mwin_eventTouchDown);
        const mwinEvent* motion = First(program, mwin_eventTouchMoved);
        const mwinEvent* up = First(program, mwin_eventTouchUp);
        CHECK(down != nullptr && motion != nullptr && down->data.touch.id == 7 &&
                  down->data.touch.position.y == 2.0f && motion->data.touch.position.x == 3.0f &&
                  up->data.touch.id == 7 && down->data.touch.pressure == -1.0f,
              "a touch goes down, moves and lifts");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "a text cursor");
        break;
    }
    default:
        AdvanceCursor(program, context);
        break;
    }
}

// Checks what the phase brought, and starts the next.
static void Advance(Program* program, mwinContext* context)
{
    Server* server = program->server;
    const mwinEvent* down = First(program, mwin_eventKeyDown);
    switch (program->phase)
    {
    case phaseCreate:
        ServerEnter(server);
        Press(server, KEY_A);
        break;
    case phaseKeys:
        CHECK(down != nullptr && down->data.key.code == mwin_codeKeyA &&
                  down->data.key.key == 'a' && !down->data.key.repeat &&
                  down->data.key.modifiers == 0 && TextIs(program, "a"),
              "a key, what it means, and its text");
        ServerModifiers(server, 1, 0);
        Press(server, KEY_A);
        break;
    case phaseShift:
        CHECK(down != nullptr && down->data.key.key == 'a' &&
                  (down->data.key.modifiers & mwin_modShift) != 0 && TextIs(program, "A"),
              "Shift changes the text, not the meaning");
        ServerModifiers(server, 0, 0);
        ServerKey(server, KEY_A, true);
        break;
    case phaseRepeat:
    {
        int repeats = CountOf(program, mwin_eventKeyDown, true);
        bool typed = program->textLength == (uint32_t)repeats + 1;
        for (uint32_t i = 0; i < program->textLength; i++)
        {
            typed = typed && program->text[i] == 'a';
        }
        CHECK(repeats >= 2 && typed, "a held key repeats, and each repeat types");
        ServerKey(server, KEY_A, false);
        // us(intl): the apostrophe is a dead acute.
        Press(server, KEY_APOSTROPHE);
        Press(server, KEY_E);
        break;
    }
    case phaseCompose:
        CHECK(TextIs(program, "\xC3\xA9"), "a dead key composes");
        ServerModifiers(server, 0, 1);
        break;
    case phaseLayout:
    {
        char name[64];
        size_t length = 0;
        CHECK(mwinMapKeyCode(context, mwin_codeKeyY) == 'z' &&
                  mwinGetKeyboardLayout(context, name, sizeof(name), &length) == mwin_success &&
                  length >= 6 && memcmp(name, "German", 6) == 0,
              "the second group is German");
        ServerKey(server, KEY_A, true);
        ServerLeave(server);
        break;
    }
    case phaseLeave:
        CHECK(CountOf(program, mwin_eventKeyUp, false) == 0, "no release comes alone");
        ServerPointerEnter(server, 10.0, 20.0);
        break;
    default:
        AdvancePointer(program, context);
        break;
    }
    program->phase += 1;
    program->count = 0;
    program->textLength = 0;
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
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 500000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    Server server;
    if (runtime == nullptr || runtime[0] == '\0' || !ServerStart(&server, "us,de", "intl,"))
    {
        return 77;
    }
    // Cursors made from images are sized by a viewport.
    ServerAddViewporter(&server);
    // The compose table of the locale.
    setenv("LANG", "en_US.UTF-8", 1);
    Program program = {.server = &server};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    return s_failures == 0 ? 0 : 1;
}
