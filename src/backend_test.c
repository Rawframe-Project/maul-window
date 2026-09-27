// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test backend: a platform with no screen. What mwinTestPost reports
// and the requests the program makes wait until the next pump, as a
// platform's messages would; the pump delivers the reports in order,
// then answers the requests in order as the answer set for their kind
// says. Carrying a request out posts the notifications a platform would
// send.

#include "allocator.h"
#include "backend.h"
#include "core.h"

#include "maul-unicode/encoding.h"
#include "maul-window/test.h"

#include <math.h>
#include <string.h>

#define KINDS (mwin_requestClipboardRead + 1)

// A request waiting for the next pump. The generations tell it from a
// later window or request in the same slots.
typedef struct Pending
{
    uint32_t slot;
    uint32_t request;
    uint32_t windowGeneration;
    uint32_t requestGeneration;
} Pending;

// Reports and their text waiting for the next pump.
#define MAX_REPORTS         1024
#define MAX_REPORT_TEXT     65536
#define MAX_REPORT_SEGMENTS 1024

// A gamepad's last rumble, and how many there were.
typedef struct Rumble
{
    float low;
    float high;
    uint32_t durationMs;
    uint32_t count;
} Rumble;

typedef struct TestPlatform
{
    Pending* pending;
    // One per gamepad slot.
    Rumble* rumbles;
    uint32_t pendingCount;
    uint32_t pendingCapacity;
    mwinEvent reports[MAX_REPORTS];
    uint32_t reportCount;
    char reportText[MAX_REPORT_TEXT];
    uint32_t reportTextUsed;
    mwinPreeditSegment reportSegments[MAX_REPORT_SEGMENTS];
    uint32_t reportSegmentsUsed;
    mwinOutcome answers[KINDS];
    // The platform's clipboard: bytes, or UTF-16 units when utf16 is set,
    // in a block of their own from the allocator.
    void* clipboard;
    size_t clipboardBytes;
    bool utf16;
    // A drop gathered in the context waits for its report.
    bool dropWaiting;
    bool hold;
    uint64_t timeNs;
    float scale;
} TestPlatform;

static TestPlatform* PlatformOf(const mwinContext* context)
{
    return context != nullptr && context->backend == &mwinTestBackend
               ? (TestPlatform*)context->backendData
               : nullptr;
}

static size_t PlatformBytes(const mwinContext* context)
{
    return sizeof(TestPlatform) +
           (size_t)context->limits.windows * context->limits.requestsPerWindow * sizeof(Pending) +
           (size_t)context->limits.gamepads * sizeof(Rumble);
}

static mwinResult Start(mwinContext* context)
{
    unsigned char* block =
        mwinAllocate(&context->allocator, PlatformBytes(context), alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, PlatformBytes(context));
    TestPlatform* platform = (TestPlatform*)block;
    platform->pending = (Pending*)(block + sizeof(TestPlatform));
    platform->pendingCapacity =
        (uint32_t)context->limits.windows * context->limits.requestsPerWindow;
    platform->rumbles = (Rumble*)(platform->pending + platform->pendingCapacity);
    platform->scale = 1.0f;
    context->backendData = platform;
    return mwin_success;
}

static void ReleaseClipboard(const mwinContext* context, TestPlatform* platform)
{
    if (platform->clipboard != nullptr)
    {
        mwinRelease(&context->allocator, platform->clipboard, platform->clipboardBytes,
                    alignof(uint16_t));
    }
    platform->clipboard = nullptr;
    platform->clipboardBytes = 0;
}

// Puts bytes on the platform's clipboard; false when there is no room.
static bool SetClipboard(const mwinContext* context, const void* data, size_t bytes, bool utf16)
{
    TestPlatform* platform = PlatformOf(context);
    void* copy = bytes > 0 ? mwinAllocate(&context->allocator, bytes, alignof(uint16_t)) : nullptr;
    if (bytes > 0 && copy == nullptr)
    {
        return false;
    }
    if (bytes > 0)
    {
        memcpy(copy, data, bytes);
    }
    ReleaseClipboard(context, platform);
    platform->clipboard = copy;
    platform->clipboardBytes = bytes;
    platform->utf16 = utf16;
    return true;
}

static void Stop(mwinContext* context)
{
    ReleaseClipboard(context, PlatformOf(context));
    mwinRelease(&context->allocator, context->backendData, PlatformBytes(context),
                alignof(max_align_t));
    context->backendData = nullptr;
}

static uint64_t Now(const mwinContext* context)
{
    return PlatformOf(context)->timeNs;
}

// Whether a waiting request is still the one it was queued as.
static bool IsCurrent(const mwinContext* context, const Pending* pending)
{
    const mwinWindow* window = &context->windows[pending->slot];
    const mwinRequest* request = &window->requests[pending->request];
    return window->status == mwin_slotLive && window->generation == pending->windowGeneration &&
           request->status == mwin_requestActive &&
           request->generation == pending->requestGeneration;
}

static void Queue(mwinContext* context, uint32_t slot, uint32_t request)
{
    TestPlatform* platform = PlatformOf(context);
    if (platform->pendingCount == platform->pendingCapacity)
    {
        // Entries of answered requests go; the active requests left fit,
        // since the list has room for every request slot.
        uint32_t kept = 0;
        for (uint32_t i = 0; i < platform->pendingCount; i++)
        {
            if (IsCurrent(context, &platform->pending[i]))
            {
                platform->pending[kept++] = platform->pending[i];
            }
        }
        platform->pendingCount = kept;
    }
    const mwinWindow* window = &context->windows[slot];
    platform->pending[platform->pendingCount++] =
        (Pending){slot, request, window->generation, window->requests[request].generation};
}

static void CreateWindow(mwinContext* context, uint32_t slot)
{
    int32_t request = mwinFindActiveRequest(&context->windows[slot],
                                            context->limits.requestsPerWindow, mwin_requestCreate);
    Queue(context, slot, (uint32_t)request);
}

static void DestroyWindow(mwinContext* context, uint32_t slot)
{
    (void)context;
    (void)slot;
}

static void Submit(mwinContext* context, uint32_t slot, uint32_t request)
{
    Queue(context, slot, request);
}

static void PostType(mwinContext* context, uint32_t slot, mwinEventType type)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = Now(context);
    mwinPost(context, slot, &event);
}

// Reports a new logical size, and the pixel size the scale gives it.
static void PostSize(mwinContext* context, uint32_t slot, mwinSize size)
{
    float scale = PlatformOf(context)->scale;
    mwinEvent event = {0};
    event.timeNs = Now(context);
    event.type = mwin_eventResized;
    event.data.size = size;
    mwinPost(context, slot, &event);
    event.type = mwin_eventPixelSizeChanged;
    event.data.pixelSize = (mwinPixelSize){(uint32_t)lroundf(size.width * scale),
                                           (uint32_t)lroundf(size.height * scale)};
    mwinPost(context, slot, &event);
}

static void PostMode(mwinContext* context, uint32_t slot, mwinWindowMode mode)
{
    mwinEvent event = {0};
    event.type = mwin_eventModeChanged;
    event.timeNs = Now(context);
    event.data.mode = mode;
    mwinPost(context, slot, &event);
}

// The primary monitor's slot, or -1 when none is connected.
static int32_t PrimaryMonitor(const mwinContext* context)
{
    mwinMonitorId primary = {0};
    size_t count = 0;
    (void)mwinGetMonitors(context, &primary, 1, &count);
    return count > 0 ? (int32_t)(primary.index1 - 1) : -1;
}

static void Create(mwinContext* context, uint32_t slot)
{
    mwinWindow* window = &context->windows[slot];
    PostType(context, slot, mwin_eventWindowCreated);
    int32_t monitor = PrimaryMonitor(context);
    if (monitor >= 0)
    {
        mwinEvent display = {0};
        display.type = mwin_eventDisplayChanged;
        display.timeNs = Now(context);
        display.data.monitor = mwinMonitorIdOf(context, (uint32_t)monitor);
        mwinPost(context, slot, &display);
    }
    mwinEvent event = {0};
    event.type = mwin_eventScaleChanged;
    event.timeNs = Now(context);
    event.data.scale = (mwinScaleChange){PlatformOf(context)->scale, window->def.size};
    mwinPost(context, slot, &event);
    PostSize(context, slot, window->def.size);
    PostMode(context, slot, window->def.mode);
    if (window->def.visible)
    {
        PostType(context, slot, mwin_eventShown);
    }
}

// Moves focus to a window, from the one that had it.
static void Focus(mwinContext* context, uint32_t slot)
{
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        if (i != slot && context->windows[i].status == mwin_slotLive &&
            context->windows[i].state.focused)
        {
            PostType(context, i, mwin_eventFocusLost);
        }
    }
    PostType(context, slot, mwin_eventFocusGained);
}

// Uses the clipboard as a platform would: a write replaces its text, a
// read takes it.
static mwinOutcome UseClipboard(mwinContext* context, mwinRequestKind kind)
{
    const TestPlatform* platform = PlatformOf(context);
    if (kind == mwin_requestClipboardWrite)
    {
        return SetClipboard(context, context->clipboardOffer, context->clipboardOfferLength, false)
                   ? mwin_outcomeDone
                   : mwin_outcomeFailed;
    }
    return platform->utf16
               ? mwinTakeClipboardUtf16(context, platform->clipboard, platform->clipboardBytes / 2)
               : mwinTakeClipboardText(context, platform->clipboard, platform->clipboardBytes);
}

// Carries out a request the answers say to do, and says how it ended.
static mwinOutcome CarryOut(mwinContext* context, uint32_t slot, const mwinRequest* request)
{
    mwinWindow* window = &context->windows[slot];
    switch (request->kind)
    {
    case mwin_requestCreate:
        Create(context, slot);
        break;
    case mwin_requestTitle:
        memmove(window->title, window->pendingTitle, window->pendingTitleLength);
        window->titleLength = window->pendingTitleLength;
        break;
    case mwin_requestSize:
        PostSize(context, slot, request->value.size);
        break;
    case mwin_requestPosition:
    {
        mwinEvent event = {0};
        event.type = mwin_eventMoved;
        event.timeNs = Now(context);
        event.data.position = request->value.position;
        mwinPost(context, slot, &event);
        break;
    }
    case mwin_requestMode:
        PostMode(context, slot, request->value.mode);
        break;
    case mwin_requestVisible:
        PostType(context, slot, request->value.visible ? mwin_eventShown : mwin_eventHidden);
        break;
    case mwin_requestFocus:
        Focus(context, slot);
        break;
    case mwin_requestTextInput:
        if (!request->value.textInput.enabled && window->state.composing)
        {
            // Leaving text input ends the composition.
            mwinEvent end = {0};
            end.type = mwin_eventImePreedit;
            end.timeNs = Now(context);
            end.data.preedit.caret = -1;
            mwinPost(context, slot, &end);
        }
        break;
    case mwin_requestVirtualKeyboard:
    {
        // The keyboard covers the lower two fifths of the window.
        mwinSize size = window->state.size;
        mwinEvent event = {0};
        event.type = mwin_eventVirtualKeyboardChanged;
        event.timeNs = Now(context);
        if ((request->value.code & 0x80u) != 0)
        {
            event.data.rect = (mwinRect){0.0f, size.height * 0.6f, size.width, size.height * 0.4f};
        }
        mwinPost(context, slot, &event);
        break;
    }
    case mwin_requestClipboardWrite:
    case mwin_requestClipboardRead:
        return UseClipboard(context, request->kind);
    default:
        break; // the cursor changes on screen, with nothing to report
    }
    return mwin_outcomeDone;
}

static bool IsLifecycle(mwinEventType type)
{
    return type >= mwin_eventSuspending && type <= mwin_eventResumed;
}

// Reports about the application rather than a window.
static bool IsGlobal(mwinEventType type)
{
    return IsLifecycle(type) || type == mwin_eventKeyboardLayoutChanged;
}

// Delivers the gathered drop to its window, or drops it with the window.
static void Deliver(mwinContext* context, const mwinEvent* report)
{
    PlatformOf(context)->dropWaiting = false;
    if (mwinFindWindow(context, report->window) != nullptr)
    {
        mwinFinishDrop(context, report->window.index1 - 1, report->data.drop.position,
                       report->timeNs);
    }
    else
    {
        mwinBeginDrop(context);
    }
}

static void Pump(mwinContext* context)
{
    TestPlatform* platform = PlatformOf(context);
    for (uint32_t i = 0; i < platform->reportCount; i++)
    {
        const mwinEvent* report = &platform->reports[i];
        if (IsGlobal(report->type))
        {
            mwinPostGlobal(context, report);
        }
        else if (report->type == mwin_eventDropped)
        {
            // Its window may have gone: the drop still ends.
            Deliver(context, report);
        }
        else if (mwinFindWindow(context, report->window) != nullptr)
        {
            mwinPost(context, report->window.index1 - 1, report);
        }
        // A platform waits for the program to handle these before it goes
        // on, as Android does for onPause.
        if (IsLifecycle(report->type) || report->type == mwin_eventSurfaceLost ||
            report->type == mwin_eventSurfaceRestored)
        {
            mwinRunCriticalFrame(context);
        }
    }
    platform->reportCount = 0;
    platform->reportTextUsed = 0;
    platform->reportSegmentsUsed = 0;
    if (platform->hold)
    {
        return;
    }
    // Answering may queue nothing new, so the list is walked once.
    uint32_t count = platform->pendingCount;
    platform->pendingCount = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        Pending pending = platform->pending[i];
        if (!IsCurrent(context, &pending))
        {
            continue;
        }
        const mwinRequest* request = &context->windows[pending.slot].requests[pending.request];
        mwinOutcome outcome = platform->answers[request->kind];
        if (outcome == mwin_outcomeDone)
        {
            outcome = CarryOut(context, pending.slot, request);
        }
        mwinComplete(context, pending.slot, pending.request, outcome);
    }
}

static mwinResult Run(mwinContext* context)
{
    return mwinRunLoop(context, Pump);
}

// The characters of the keys from Enter to Slash on a US layout, in code
// order; a space marks a key that types nothing.
static const char s_punctuation[] = "     -=[]\\\\;'`,./";

// A US layout.
static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    (void)context;
    if (code >= mwin_codeKeyA && code <= mwin_codeKeyZ)
    {
        return 'a' + (code - mwin_codeKeyA);
    }
    if (code >= mwin_codeDigit1 && code <= mwin_codeDigit0)
    {
        return code == mwin_codeDigit0 ? '0' : '1' + (code - mwin_codeDigit1);
    }
    if (code == mwin_codeSpace)
    {
        return ' ';
    }
    if (code >= mwin_codeMinus && code <= mwin_codeSlash)
    {
        return (mwinKey)(unsigned char)s_punctuation[code - mwin_codeEnter];
    }
    return MWIN_KEY_NAMED | code;
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    (void)context;
    static const char name[] = "English (US)";
    size_t length = sizeof(name) - 1;
    if (capacity > 0)
    {
        memcpy(buffer, name, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    (void)context;
    (void)slot;
    out->platform = mwin_platformTest;
}

static mwinResult RumbleGamepad(mwinContext* context, uint32_t slot, float low, float high,
                                uint32_t durationMs)
{
    Rumble* rumble = &PlatformOf(context)->rumbles[slot];
    *rumble = (Rumble){low, high, durationMs, rumble->count + 1};
    return mwin_success;
}

const mwinBackendOps mwinTestBackend = {
    Start,      Stop,           Run,           CreateWindow,  DestroyWindow, Submit, Now,
    MapKeyCode, KeyboardLayout, NativeHandles, RumbleGamepad,
};

mwinResult mwinTestSetAnswer(mwinContext* context, mwinRequestKind kind, mwinOutcome outcome)
{
    if (context == nullptr || kind >= KINDS || outcome == mwin_outcomeSuperseded ||
        outcome == mwin_outcomeCancelled || outcome > mwin_outcomeFailed)
    {
        return mwin_errorInvalid;
    }
    TestPlatform* platform = PlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    platform->answers[kind] = outcome;
    return mwin_success;
}

mwinResult mwinTestHold(mwinContext* context, bool hold)
{
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    TestPlatform* platform = PlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    platform->hold = hold;
    return mwin_success;
}

// Whether a record type is one a test may report: not the core's own,
// and not what the mwinTest setters report.
static bool IsReportable(mwinEventType type)
{
    return type != mwin_eventNone && type != mwin_eventWindowCreated &&
           type != mwin_eventWindowDestroyed && type != mwin_eventRequestCompleted &&
           type != mwin_eventInputStateReset &&
           (type <= mwin_eventImePreedit ||
            (type >= mwin_eventDragEntered && type <= mwin_eventDragLeft)) &&
           !(type >= mwin_eventMonitorAdded && type <= mwin_eventMonitorChanged) &&
           !(type >= mwin_eventThemeChanged && type <= mwin_eventLocaleChanged);
}

static bool HasText(mwinEventType type)
{
    return type == mwin_eventTextInput || type == mwin_eventImePreedit;
}

// Whether a report's text and segments are well formed.
static bool IsTextValid(const mwinEvent* event)
{
    if (!HasText(event->type))
    {
        return true;
    }
    const mwinPreeditEvent* preedit = &event->data.preedit;
    bool segmentsValid = event->type != mwin_eventImePreedit ||
                         (preedit->segmentCount <= MWIN_MAX_PREEDIT_SEGMENTS &&
                          (preedit->segments != nullptr || preedit->segmentCount == 0));
    return segmentsValid && (preedit->text != nullptr || preedit->length == 0) &&
           muniValidateUtf8(preedit->text, preedit->length).status == muni_success;
}

// Copies a report, its text and its segments into the platform's queue.
static mwinResult QueueReport(TestPlatform* platform, const mwinEvent* event)
{
    uint32_t length = HasText(event->type) ? event->data.text.length : 0;
    uint32_t segments = event->type == mwin_eventImePreedit ? event->data.preedit.segmentCount : 0;
    if (platform->reportCount == MAX_REPORTS ||
        MAX_REPORT_TEXT - platform->reportTextUsed < length ||
        MAX_REPORT_SEGMENTS - platform->reportSegmentsUsed < segments)
    {
        return mwin_errorCapacity;
    }
    mwinEvent* record = &platform->reports[platform->reportCount++];
    *record = *event;
    record->timeNs = platform->timeNs;
    if (HasText(event->type))
    {
        char* copy = platform->reportText + platform->reportTextUsed;
        if (length > 0)
        {
            memcpy(copy, event->data.text.text, length);
        }
        record->data.text.text = copy;
        platform->reportTextUsed += length;
    }
    if (segments > 0)
    {
        mwinPreeditSegment* copy = platform->reportSegments + platform->reportSegmentsUsed;
        memcpy(copy, event->data.preedit.segments, segments * sizeof(mwinPreeditSegment));
        record->data.preedit.segments = copy;
        platform->reportSegmentsUsed += segments;
    }
    return mwin_success;
}

mwinResult mwinTestPost(mwinContext* context, const mwinEvent* event)
{
    if (context == nullptr || event == nullptr || !IsReportable(event->type) || !IsTextValid(event))
    {
        return mwin_errorInvalid;
    }
    TestPlatform* platform = PlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if ((!IsGlobal(event->type) && mwinFindWindow(context, event->window) == nullptr) ||
        (event->type == mwin_eventDisplayChanged &&
         mwinFindMonitor(context, event->data.monitor) < 0))
    {
        return mwin_errorStale;
    }
    return QueueReport(platform, event);
}

mwinResult mwinTestSetTime(mwinContext* context, uint64_t timeNs)
{
    TestPlatform* platform = PlatformOf(context);
    if (context == nullptr || (platform != nullptr && timeNs < platform->timeNs))
    {
        return mwin_errorInvalid;
    }
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    platform->timeNs = timeNs;
    return mwin_success;
}

mwinResult mwinTestSetScale(mwinContext* context, float scale)
{
    if (context == nullptr || !isfinite(scale) || scale <= 0.0f)
    {
        return mwin_errorInvalid;
    }
    TestPlatform* platform = PlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    platform->scale = scale;
    return mwin_success;
}

mwinResult mwinTestGetTitle(const mwinContext* context, mwinWindowId window, char* buffer,
                            size_t capacity, size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity != 0))
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    const mwinWindow* found = mwinFindWindow(context, window);
    if (found == nullptr)
    {
        return mwin_errorStale;
    }
    size_t length = found->titleLength;
    if (capacity > 0)
    {
        memcpy(buffer, found->title, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinTestAddMonitor(mwinContext* context, const mwinMonitorInfo* info,
                              mwinMonitorId* monitorOut)
{
    if (context == nullptr || info == nullptr || monitorOut == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int32_t slot = mwinAddMonitor(context, info, Now(context));
    if (slot < 0)
    {
        return mwin_errorCapacity;
    }
    *monitorOut = mwinMonitorIdOf(context, (uint32_t)slot);
    return mwin_success;
}

mwinResult mwinTestChangeMonitor(mwinContext* context, mwinMonitorId monitor,
                                 const mwinMonitorInfo* info)
{
    if (context == nullptr || info == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int32_t slot = mwinFindMonitor(context, monitor);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    mwinChangeMonitor(context, (uint32_t)slot, info, Now(context));
    return mwin_success;
}

mwinResult mwinTestRemoveMonitor(mwinContext* context, mwinMonitorId monitor)
{
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int32_t slot = mwinFindMonitor(context, monitor);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    mwinRemoveMonitor(context, (uint32_t)slot, Now(context));
    return mwin_success;
}

mwinResult mwinTestSetSystemFacts(mwinContext* context, const mwinSystemFacts* facts)
{
    if (context == nullptr || facts == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    mwinSetSystemFacts(context, facts, Now(context));
    return mwin_success;
}

mwinResult mwinTestSetLocales(mwinContext* context, const char* locales, size_t length)
{
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (length > context->limits.localeBytes)
    {
        return mwin_errorCapacity;
    }
    return mwinSetLocales(context, locales, length, Now(context)) ? mwin_success
                                                                  : mwin_errorInvalid;
}

// The slot of a gamepad of a test context, or why there is none.
static mwinResult FindTestGamepad(const mwinContext* context, mwinGamepadId gamepad,
                                  int32_t* slotOut)
{
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    *slotOut = mwinFindGamepad(context, gamepad);
    return *slotOut < 0 ? mwin_errorStale : mwin_success;
}

mwinResult mwinTestAddGamepad(mwinContext* context, const mwinGamepadInfo* info,
                              mwinGamepadId* gamepadOut)
{
    if (context == nullptr || info == nullptr || gamepadOut == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int32_t slot = mwinAddGamepad(context, info, Now(context));
    if (slot < 0)
    {
        return mwin_errorCapacity;
    }
    PlatformOf(context)->rumbles[slot] = (Rumble){0};
    *gamepadOut = mwinGamepadIdOf(context, (uint32_t)slot);
    return mwin_success;
}

mwinResult mwinTestChangeGamepad(mwinContext* context, mwinGamepadId gamepad,
                                 const mwinGamepadInfo* info)
{
    int32_t slot = -1;
    mwinResult status =
        info != nullptr ? FindTestGamepad(context, gamepad, &slot) : mwin_errorInvalid;
    if (status == mwin_success)
    {
        mwinChangeGamepad(context, (uint32_t)slot, info, Now(context));
    }
    return status;
}

mwinResult mwinTestRemoveGamepad(mwinContext* context, mwinGamepadId gamepad)
{
    int32_t slot = -1;
    mwinResult status = FindTestGamepad(context, gamepad, &slot);
    if (status == mwin_success)
    {
        mwinRemoveGamepad(context, (uint32_t)slot, Now(context));
    }
    return status;
}

mwinResult mwinTestGamepadButton(mwinContext* context, mwinGamepadId gamepad, uint8_t button,
                                 bool down)
{
    int32_t slot = -1;
    mwinResult status = FindTestGamepad(context, gamepad, &slot);
    if (status == mwin_success)
    {
        mwinPostGamepadButton(context, (uint32_t)slot, button, down, Now(context));
    }
    return status;
}

mwinResult mwinTestGamepadAxis(mwinContext* context, mwinGamepadId gamepad, uint8_t axis,
                               float value)
{
    int32_t slot = -1;
    mwinResult status = FindTestGamepad(context, gamepad, &slot);
    if (status == mwin_success)
    {
        mwinPostGamepadAxis(context, (uint32_t)slot, axis, value, Now(context));
    }
    return status;
}

mwinResult mwinTestGetRumble(const mwinContext* context, mwinGamepadId gamepad, float* lowOut,
                             float* highOut, uint32_t* durationMsOut, uint32_t* countOut)
{
    int32_t slot = -1;
    mwinResult status =
        lowOut != nullptr && highOut != nullptr && durationMsOut != nullptr && countOut != nullptr
            ? FindTestGamepad(context, gamepad, &slot)
            : mwin_errorInvalid;
    if (status == mwin_success)
    {
        const Rumble* rumble = &PlatformOf(context)->rumbles[slot];
        *lowOut = rumble->low;
        *highOut = rumble->high;
        *durationMsOut = rumble->durationMs;
        *countOut = rumble->count;
    }
    return status;
}

mwinResult mwinTestSetClipboard(mwinContext* context, const char* bytes, size_t length)
{
    if (context == nullptr || (bytes == nullptr && length != 0))
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    return SetClipboard(context, bytes, length, false) ? mwin_success : mwin_errorCapacity;
}

mwinResult mwinTestSetClipboardUtf16(mwinContext* context, const uint16_t* units, size_t length)
{
    if (context == nullptr || (units == nullptr && length != 0))
    {
        return mwin_errorInvalid;
    }
    if (PlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    return SetClipboard(context, units, length * 2, true) ? mwin_success : mwin_errorCapacity;
}

mwinResult mwinTestGetClipboard(const mwinContext* context, char* buffer, size_t capacity,
                                size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwin_errorInvalid;
    }
    const TestPlatform* platform = PlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    size_t length = platform->clipboardBytes;
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, platform->clipboard, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinTestDrop(mwinContext* context, mwinWindowId window, mwinPosition position,
                        const char* files, size_t filesLength, const char* text, size_t textLength)
{
    if (context == nullptr || (files == nullptr && filesLength != 0) ||
        (filesLength > 0 && files[filesLength - 1] != '\0'))
    {
        return mwin_errorInvalid;
    }
    TestPlatform* platform = PlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (mwinFindWindow(context, window) == nullptr)
    {
        return mwin_errorStale;
    }
    if (platform->dropWaiting)
    {
        return mwin_errorState;
    }
    mwinEvent report = {.type = mwin_eventDropped, .window = window, .timeNs = Now(context)};
    report.data.drop.position = position;
    mwinResult status = QueueReport(platform, &report);
    if (status != mwin_success)
    {
        return status;
    }
    mwinBeginDrop(context);
    for (size_t at = 0; at < filesLength; at += strlen(files + at) + 1)
    {
        mwinAddDroppedFile(context, files + at, strlen(files + at));
    }
    if (text != nullptr)
    {
        mwinSetDroppedText(context, text, textLength);
    }
    platform->dropWaiting = true;
    return mwin_success;
}
