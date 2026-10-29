// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the web backend's reading of the records the page queues for it
// (mwinWebHandleRecord and the web_*.c handlers): keys, text, the mouse,
// the wheel, locked motion, touches, pens, focus, sizes, scales,
// fullscreen, compositions, the clipboard's answers, drags and drops. The
// page holds other scripts, which can reach the queue and the strings
// beside it, so every field of a record and every string is the input's.
// The web backend's C files are built for the host, its page's functions
// weak declarations (web_js.h), and those the records reach are defined
// here from the input, each keeping what its JavaScript promises (a
// length within the room given, or -1). Each input runs on a context of
// the test backend: its windows' slots hold the web backend's windows,
// and the requests the page answers (the clipboard, the cursor mode,
// fullscreen) stay waiting for its records. The input is a script of
// records, such requests, windows destroyed, and drains.
//
// Every request completes exactly once with an outcome there is; a read's
// text is UTF-8 within the clipboard limit; a drop's paths and text are
// UTF-8 within their limits; text and compositions are UTF-8 within the
// window's limit, a composition's offsets on character boundaries;
// positions, sizes, scales, wheel turns and motion are finite, a scale
// above zero and a size not below it; pressures are from 0 to 1 (a
// touch's may be -1), tilts finite; buttons, modifiers, key codes, what
// keys mean and the facts are ones there are; and the sanitizers see no read past an array and no
// conversion of a number that does not fit.

#include "web.h"
#include "web_page.h"

#include "maul-unicode/encoding.h"
#include "maul-window/clipboard.h"
#include "maul-window/drop.h"
#include "maul-window/input.h"
#include "maul-window/monitor.h"
#include "maul-window/system.h"
#include "maul-window/window.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOWS 3
// The context's window slots: a destroyed window's slot is free again
// only once its end is drained, after its replacement is made.
#define SLOTS   8
#define LIMIT   1024u
#define TEXT    64u
#define FILES   8u
#define TRACKED 128

static const uint8_t* s_data;
static size_t s_size;

static void ExpectAt(bool condition, const char* text, int line)
{
    if (!condition)
    {
        (void)fprintf(stderr, "expected at line %d: %s\n", line, text);
        abort();
    }
}

#define Expect(condition) ExpectAt((condition), #condition, __LINE__)

static uint8_t Byte(void)
{
    if (s_size == 0)
    {
        return 0;
    }
    s_size--;
    return *s_data++;
}

static mwinWebPlatform s_platform;
static mwinWebWindow s_windows[SLOTS];
static char s_text[TEXT + 1];
static mwinWindowId s_ids[WINDOWS];
static mwinBackendOps s_ops;
static const mwinBackendOps* s_test;

static struct
{
    mwinRequestId id;
    mwinRequestKind kind;
    uint32_t completions;
} s_requests[TRACKED];
static uint32_t s_made;

// Numbers a record's fields take: ones the page sends, edges, and ones
// no browser would.
static const int32_t s_integers[] = {0,        1,        2,  3,   4,         5,        6,    7,
                                     8,        31,       32, 255, 256,       0x7F,     0x41, 0xD800,
                                     0x10FFFF, 0x110000, -1, -2,  INT32_MAX, INT32_MIN};
static const float s_floats[] = {0.0f,   1.0f,   0.5f,   -1.0f, 2.0f,   100.0f,
                                 255.0f, 256.0f, 65.0f,  1e30f, -1e30f, 3.4e38f,
                                 4.3e9f, -0.0f,  1e-40f, 90.0f, -90.0f, 0.25f};

static int32_t Integer(void)
{
    uint8_t pick = Byte();
    return pick < 0xF0 ? s_integers[pick % (sizeof(s_integers) / sizeof(s_integers[0]))]
                       : (int32_t)((uint32_t)Byte() << 24 | (uint32_t)Byte() << 16 |
                                   (uint32_t)Byte() << 8 | Byte());
}

static float Number(void)
{
    uint8_t pick = Byte();
    switch (pick >> 6)
    {
    case 3:
        // Infinities and a NaN, which a script can put in the queue.
        return (pick & 3) == 0 ? INFINITY : (pick & 3) == 1 ? -INFINITY : NAN;
    default:
        return s_floats[pick % (sizeof(s_floats) / sizeof(s_floats[0]))];
    }
}

// A slot for a record: a window's, or one no window has, or none.
static int32_t Slot(void)
{
    static const int32_t others[] = {-1, SLOTS, SLOTS - 1, INT32_MAX, INT32_MIN, 1000};
    uint8_t pick = Byte();
    if (pick < 0xC0)
    {
        return (int32_t)s_ids[pick % WINDOWS].index1 - 1;
    }
    return others[pick % (sizeof(others) / sizeof(others[0]))];
}

// The page's functions the records reach, from the input. Those the C
// files declare for themselves are declared here as they are.
int mwinPageTakeString(const mwinContext* context, char* out, uint32_t capacity);
uint32_t mwinPageClipboardWaiting(const mwinContext* context);
void mwinPageClipboardTake(const mwinContext* context, char* out, uint32_t length);
uint32_t mwinPageDropNameCount(const mwinContext* context);
int mwinPageDropName(const mwinContext* context, uint32_t index, char* out, uint32_t capacity);
int mwinPageDropTextLength(const mwinContext* context);
void mwinPageTakeDrop(const mwinContext* context, char* out, uint32_t length);
void mwinPageSetCursor(const mwinContext* context, uint32_t slot, int shape, bool hidden,
                       uint32_t image);

static uint8_t s_bytes[4096];

// Bytes of the input's, as a string the page hands over: any bytes, which
// a script can make; the length given, or as much input as is left.
static uint32_t Bytes(uint32_t length)
{
    uint32_t i = 0;
    for (; i < length && i < sizeof(s_bytes); i++)
    {
        uint8_t shape = Byte();
        // Mostly letters and a multi-byte character, now and then a raw byte.
        s_bytes[i] = shape < 0xC0 ? (uint8_t)('a' + shape % 26) : shape;
    }
    return i;
}

// A length for a string: small, at a limit, or past it.
static uint32_t Length(uint32_t limit)
{
    uint8_t pick = Byte();
    switch (pick & 7)
    {
    case 0:
        return limit;
    case 1:
        return limit + 1;
    case 2:
        return limit - 1;
    case 3:
        return limit * 2;
    default:
        return pick >> 3;
    }
}

double mwinWebNow(void)
{
    return 1000.0;
}

void mwinWebScreen(float* out)
{
    for (int i = 0; i < 4; i++)
    {
        out[i] = Number();
    }
}

bool mwinWebHighDynamicRange(void)
{
    return (Byte() & 1) != 0;
}

// What the page's functions give for the facts: matchMedia's answers,
// and the power source another script can set to anything.
int mwinWebTheme(void)
{
    return Integer();
}

bool mwinWebReducedMotion(void)
{
    return (Byte() & 1) != 0;
}

int mwinWebOnBattery(const mwinContext* context)
{
    (void)context;
    return Integer();
}

uint32_t mwinWebLocales(char* out, uint32_t capacity)
{
    uint32_t length = Length(32);
    uint32_t made = Bytes(length < capacity ? length : capacity - 1);
    memcpy(out, s_bytes, made);
    out[made] = '\0';
    // A list that does not fit says its length and writes what fits.
    return length < capacity ? made : length;
}

int mwinPageTakeString(const mwinContext* context, char* out, uint32_t capacity)
{
    (void)context;
    uint32_t length = Length(TEXT);
    if (length >= capacity)
    {
        return -1;
    }
    length = Bytes(length);
    memcpy(out, s_bytes, length);
    out[length] = '\0';
    return (int)length;
}

static uint32_t s_waiting;

uint32_t mwinPageClipboardWaiting(const mwinContext* context)
{
    (void)context;
    s_waiting = Length(LIMIT);
    return s_waiting;
}

// Writes exactly the length the backend made room for, which is what
// it was told is waiting.
void mwinPageClipboardTake(const mwinContext* context, char* out, uint32_t length)
{
    (void)context;
    if (out == nullptr)
    {
        Expect(length == 0);
        return;
    }
    Expect(length == s_waiting);
    for (uint32_t at = 0; at < length;)
    {
        uint32_t made = Bytes(length - at);
        if (made == 0)
        {
            memset(out + at, 'x', length - at);
            break;
        }
        memcpy(out + at, s_bytes, made);
        at += made;
    }
}

uint32_t mwinPageDropNameCount(const mwinContext* context)
{
    (void)context;
    return Byte() % (FILES + 3);
}

int mwinPageDropName(const mwinContext* context, uint32_t index, char* out, uint32_t capacity)
{
    (void)context;
    (void)index;
    return mwinPageTakeString(context, out, capacity);
}

static int s_dropText;

int mwinPageDropTextLength(const mwinContext* context)
{
    (void)context;
    s_dropText = (Byte() & 7) == 0 ? -1 : (int)Length(LIMIT);
    return s_dropText;
}

void mwinPageTakeDrop(const mwinContext* context, char* out, uint32_t length)
{
    (void)context;
    if (out == nullptr)
    {
        Expect(length == 0);
        return;
    }
    Expect(s_dropText > 0 && length == (uint32_t)s_dropText);
    uint32_t made = Bytes(length);
    memcpy(out, s_bytes, made);
    memset(out + made, 'y', (size_t)length - made);
}

// A locked pointer's answer shows the cursor the program asked for.
void mwinPageSetCursor(const mwinContext* context, uint32_t slot, int shape, bool hidden,
                       uint32_t image)
{
    (void)context;
    (void)shape;
    (void)hidden;
    (void)image;
    Expect(slot < SLOTS);
}

// The test backend hands over the requests the page answers with its
// records, and leaves them waiting as the page would.
static void Submit(mwinContext* context, uint32_t slot, uint32_t index)
{
    context->backend = s_test;
    (void)slot;
    (void)index;
}

static uint64_t Now(const mwinContext* context)
{
    mwinContext* own = (mwinContext*)context;
    own->backend = s_test;
    uint64_t now = s_test->now(own);
    own->backend = &s_ops;
    return now;
}

static void Request(mwinContext* context, mwinWindowId window, uint8_t op)
{
    static const char text[] = "copied";
    static const char mime[] = "image/png";
    static const mwinClipboardItem items[] = {{mime, sizeof(mime) - 1, text, sizeof(text) - 1}};
    mwinRequestId id = {0};
    mwinResult result = mwin_errorInvalid;
    mwinRequestKind kind = mwin_requestClipboardRead;
    context->backend = &s_ops;
    switch ((op >> 4) % 6)
    {
    case 0:
        result = mwinWindow_RequestClipboardRead(context, window, &id);
        break;
    case 1:
        kind = mwin_requestClipboardWrite;
        result = mwinWindow_RequestClipboardWrite(context, window, text, sizeof(text) - 1, &id);
        break;
    case 2:
        kind = mwin_requestClipboardReadData;
        result = mwinWindow_RequestClipboardReadData(context, window, mime, sizeof(mime) - 1, &id);
        break;
    case 3:
        kind = mwin_requestClipboardWriteData;
        result = mwinWindow_RequestClipboardWriteData(context, window, items, 1, &id);
        break;
    case 4:
        kind = mwin_requestCursorMode;
        result = mwinWindow_RequestCursorMode(
            context, window, (op & 0x40) != 0 ? mwin_cursorCaptured : mwin_cursorVisible, &id);
        break;
    default:
        kind = mwin_requestMode;
        result = mwinWindow_RequestMode(
            context, window, (op & 0x40) != 0 ? mwin_modeBorderlessFullscreen : mwin_modeWindowed,
            &id);
        break;
    }
    context->backend = s_test;
    if (result == mwin_success && s_made < TRACKED)
    {
        s_requests[s_made++] = (typeof(s_requests[0])){id, kind, 0};
    }
}

static void Record(const mwinContext* context, uint8_t op)
{
    mwinWebRecord record = {0};
    uint8_t kind = Byte();
    record.kind = kind < 0xF0 ? kind % 28 : Integer();
    record.slot = Slot();
    record.code = Integer();
    // A clipboard answer names its window's generation.
    bool generation =
        record.kind == mwin_webClipboardWritten || record.kind == mwin_webClipboardRead;
    record.extra = generation && (op & 0x10) != 0 && record.slot >= 0 && record.slot < SLOTS
                       ? (int32_t)context->windows[record.slot].generation
                       : Integer();
    float* numbers[] = {&record.x, &record.y, &record.z, &record.w, &record.v, &record.u};
    for (int i = 0; i < 6; i++)
    {
        *numbers[i] = Number();
    }
    record.timeMs = (op & 0x20) != 0 ? 1.0e300 : (op & 0x40) != 0 ? -5.0 : 10.0;
    mwinWebHandleRecord(&s_platform, &record);
}

static bool IsUtf8(const char* text, size_t length)
{
    return length == 0 ||
           (text != nullptr && muniValidateUtf8(text, length).status == muni_success);
}

static bool OnBoundary(const char* text, uint32_t length, uint32_t at)
{
    return at <= length && (at == length || ((unsigned char)text[at] & 0xC0u) != 0x80u);
}

static bool Finite(mwinPosition position)
{
    return isfinite(position.x) && isfinite(position.y);
}

static bool Unit(float value)
{
    return value >= 0.0f && value <= 1.0f;
}

static void CheckPreedit(const mwinPreeditEvent* preedit)
{
    const char* text = preedit->text;
    uint32_t length = preedit->length;
    Expect(length <= TEXT && IsUtf8(text, length));
    Expect(preedit->caret == -1 || OnBoundary(text, length, (uint32_t)preedit->caret));
    Expect(preedit->selectionStart <= preedit->selectionEnd &&
           OnBoundary(text, length, preedit->selectionStart) &&
           OnBoundary(text, length, preedit->selectionEnd));
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        const mwinPreeditSegment* segment = &preedit->segments[i];
        Expect(segment->start <= length && segment->length <= length - segment->start &&
               OnBoundary(text, length, segment->start) &&
               OnBoundary(text, length, segment->start + segment->length));
    }
}

static void CheckKey(const mwinKeyEvent* key)
{
    Expect(key->code < 256 && (key->modifiers & ~MWIN_WEB_MODIFIERS) == 0);
    mwinKey meaning = key->key;
    bool named = (meaning & MWIN_KEY_NAMED) != 0;
    Expect(named ? (meaning & ~MWIN_KEY_NAMED) < 256
                 : meaning <= 0x10FFFF && (meaning < 0xD800 || meaning > 0xDFFF));
}

static void CheckFound(const mwinContext* context, const mwinCompletion* completion)
{
    static char found[LIMIT];
    size_t length = 0;
    mwinResult result = mwin_errorStale;
    if (completion->kind == mwin_requestClipboardRead)
    {
        result = mwinRequest_GetClipboardText(context, completion->request, found, LIMIT, &length);
        Expect(result != mwin_success || IsUtf8(found, length));
    }
    else if (completion->kind == mwin_requestClipboardReadData)
    {
        result = mwinRequest_GetClipboardData(context, completion->request, found, LIMIT, &length);
    }
    Expect(result != mwin_success || length <= LIMIT);
}

static void CheckDrop(const mwinContext* context, const mwinDropEvent* drop)
{
    static char bytes[LIMIT + 1];
    size_t length = 0;
    Expect(Finite(drop->position));
    // A drop a later one replaced is stale: the records are drained now
    // and then.
    mwinResult files =
        mwinContext_GetDroppedFiles(context, drop->drop, bytes, sizeof(bytes), &length);
    if (files == mwin_errorStale)
    {
        return;
    }
    Expect(files == mwin_success && length <= LIMIT);
    uint32_t count = 0;
    for (size_t at = 0; at < length; count++)
    {
        const char* end = memchr(bytes + at, '\0', length - at);
        Expect(end != nullptr);
        size_t path = (size_t)(end - (bytes + at));
        Expect(path > 0 && IsUtf8(bytes + at, path));
        at += path + 1;
    }
    Expect(count == drop->fileCount && count <= FILES);
    Expect(mwinContext_GetDroppedText(context, drop->drop, bytes, sizeof(bytes), &length) ==
               mwin_success &&
           length == drop->textLength && length <= LIMIT && IsUtf8(bytes, length));
}

static void CheckCompletion(const mwinContext* context, const mwinCompletion* completion)
{
    Expect(completion->outcome <= mwin_outcomeTooLarge);
    for (uint32_t i = 0; i < s_made; i++)
    {
        if (memcmp(&s_requests[i].id, &completion->request, sizeof(mwinRequestId)) == 0)
        {
            Expect(s_requests[i].kind == completion->kind && s_requests[i].completions == 0);
            s_requests[i].completions++;
            if (completion->outcome == mwin_outcomeDone)
            {
                CheckFound(context, completion);
            }
        }
    }
}

static void CheckMonitors(const mwinContext* context)
{
    mwinMonitorId monitors[4];
    size_t count = 0;
    Expect(mwinContext_GetMonitors(context, monitors, 4, &count) == mwin_success);
    for (size_t i = 0; i < count && i < 4; i++)
    {
        mwinMonitorInfo info;
        Expect(mwinMonitor_GetInfo(context, monitors[i], &info) == mwin_success);
        Expect(isfinite(info.scale) && info.scale > 0.0f);
    }
}

static void CheckFacts(const mwinContext* context)
{
    mwinSystemFacts facts;
    Expect(mwinContext_GetSystemFacts(context, &facts) == mwin_success);
    Expect(facts.theme <= mwin_themeDark && facts.onBattery <= mwin_yes &&
           facts.lowPower <= mwin_yes);
}

static void Drain(mwinContext* context)
{
    mwinEvent event;
    while (mwinContext_NextEvent(context, &event) == mwin_success)
    {
        const typeof(event.data)* data = &event.data;
        switch (event.type)
        {
        case mwin_eventTextInput:
            Expect(data->text.length <= TEXT && IsUtf8(data->text.text, data->text.length));
            break;
        case mwin_eventImePreedit:
            CheckPreedit(&data->preedit);
            break;
        case mwin_eventKeyDown:
        case mwin_eventKeyUp:
            CheckKey(&data->key);
            break;
        case mwin_eventCursorMoved:
        case mwin_eventCursorEntered:
        case mwin_eventCursorLeft:
        case mwin_eventButtonDown:
        case mwin_eventButtonUp:
            Expect(Finite(data->pointer.position) && data->pointer.button <= mwin_buttonForward &&
                   data->pointer.buttons < 32 &&
                   (data->pointer.modifiers & ~MWIN_WEB_MODIFIERS) == 0);
            break;
        case mwin_eventWheel:
            Expect(isfinite(data->wheel.x) && isfinite(data->wheel.y));
            break;
        case mwin_eventRawPointerDelta:
            Expect(isfinite(data->delta.x) && isfinite(data->delta.y));
            break;
        case mwin_eventTouchDown:
        case mwin_eventTouchMoved:
        case mwin_eventTouchUp:
        case mwin_eventTouchCancelled:
            Expect(Finite(data->touch.position) &&
                   (Unit(data->touch.pressure) || data->touch.pressure == -1.0f));
            break;
        case mwin_eventPenMoved:
        case mwin_eventPenDown:
        case mwin_eventPenUp:
        case mwin_eventPenButtonDown:
        case mwin_eventPenButtonUp:
            Expect(Finite(data->pen.position) && Unit(data->pen.pressure) &&
                   isfinite(data->pen.tiltX) && isfinite(data->pen.tiltY));
            break;
        case mwin_eventDragEntered:
        case mwin_eventDragMoved:
        case mwin_eventDragLeft:
            Expect(Finite(data->drag.position) &&
                   (data->drag.contents & ~(mwin_dragFiles | mwin_dragText)) == 0);
            break;
        case mwin_eventDropped:
            CheckDrop(context, &data->drop);
            break;
        case mwin_eventResized:
            Expect(isfinite(data->size.width) && isfinite(data->size.height) &&
                   data->size.width >= 0.0f && data->size.height >= 0.0f);
            break;
        case mwin_eventScaleChanged:
            Expect(isfinite(data->scale.scale) && data->scale.scale > 0.0f &&
                   isfinite(data->scale.suggestedSize.width) &&
                   isfinite(data->scale.suggestedSize.height));
            break;
        case mwin_eventThemeChanged:
        case mwin_eventPowerChanged:
            CheckFacts(context);
            break;
        case mwin_eventMonitorAdded:
        case mwin_eventMonitorChanged:
            CheckMonitors(context);
            break;
        case mwin_eventRequestCompleted:
            CheckCompletion(context, &data->completion);
            break;
        default:
            break;
        }
    }
}

static void Open(mwinContext* context, uint32_t i)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    Expect(mwinCreateWindow(context, &def, &s_ids[i], nullptr) == mwin_success);
    mwinWebWindow* window = &s_windows[s_ids[i].index1 - 1];
    *window = (mwinWebWindow){.selectorLength = 1, .open = true, .scale = s_platform.scale};
    window->selector[0] = '#';
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    s_made = 0;
    memset(s_windows, 0, sizeof(s_windows));
    s_platform = (mwinWebPlatform){
        .context = context, .windows = s_windows, .monitor = -1, .scale = 1.0f, .text = s_text};
    s_test = context->backend;
    s_ops = *s_test;
    s_ops.submit = Submit;
    s_ops.now = Now;
    for (uint32_t i = 0; i < WINDOWS; i++)
    {
        Open(context, i);
    }
    while (s_size > 0)
    {
        uint8_t op = Byte();
        uint32_t i = (op >> 4) % WINDOWS;
        switch (op & 7)
        {
        case 5:
            Request(context, s_ids[i], op);
            break;
        case 6:
            // A window goes while the page may still report on it.
            s_windows[s_ids[i].index1 - 1] = (mwinWebWindow){0};
            Expect(mwinDestroyWindow(context, s_ids[i]) == mwin_success);
            Open(context, i);
            // Its end drained, the old slot is free for the next.
            Drain(context);
            break;
        case 7:
            Drain(context);
            break;
        default:
            Record(context, op);
            break;
        }
    }
    // The windows go, and with them the requests the page never answered.
    for (uint32_t i = 0; i < WINDOWS; i++)
    {
        Expect(mwinDestroyWindow(context, s_ids[i]) == mwin_success);
    }
    Drain(context);
    for (uint32_t r = 0; r < s_made; r++)
    {
        Expect(s_requests[r].completions == 1);
    }
    return mwin_success;
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    (void)context;
    (void)user;
    return mwin_frameStop;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    s_data = data;
    s_size = size;
    mwinAppDef def = mwinDefaultAppDef();
    def.context.backend = mwin_backendTest;
    def.context.limits.windows = SLOTS;
    def.context.limits.textBytesPerWindow = TEXT;
    def.context.limits.clipboardBytes = LIMIT;
    def.context.limits.dropBytes = LIMIT;
    def.context.limits.droppedFiles = FILES;
    def.init = Init;
    def.frame = Frame;
    Expect(mwinRun(&def) == mwin_success);
    return 0;
}
