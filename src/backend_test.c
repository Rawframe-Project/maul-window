// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test backend: a platform with no screen. Requests wait in a list
// until the next pump, which answers them in order as the answer set for
// their kind says; carrying one out posts the notifications a platform
// would send. Its reports come from mwinTestPost.

#include "allocator.h"
#include "backend.h"
#include "core.h"

#include "maul-window/test.h"

#include <math.h>
#include <string.h>

#define KINDS (mwin_requestFocus + 1)

// A request waiting for the next pump. The generations tell it from a
// later window or request in the same slots.
typedef struct Pending
{
    uint32_t slot;
    uint32_t request;
    uint32_t windowGeneration;
    uint32_t requestGeneration;
} Pending;

typedef struct TestPlatform
{
    Pending* pending;
    uint32_t pendingCount;
    uint32_t pendingCapacity;
    mwinOutcome answers[KINDS];
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
           (size_t)context->limits.windows * context->limits.requestsPerWindow * sizeof(Pending);
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
    platform->scale = 1.0f;
    context->backendData = platform;
    return mwin_success;
}

static void Stop(mwinContext* context)
{
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

static void Create(mwinContext* context, uint32_t slot)
{
    mwinWindow* window = &context->windows[slot];
    PostType(context, slot, mwin_eventWindowCreated);
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

// Carries out a request the answers say to do.
static void CarryOut(mwinContext* context, uint32_t slot, const mwinRequest* request)
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
    default:
        Focus(context, slot);
        break;
    }
}

static void Pump(mwinContext* context)
{
    TestPlatform* platform = PlatformOf(context);
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
            CarryOut(context, pending.slot, request);
        }
        mwinComplete(context, pending.slot, pending.request, outcome);
    }
}

static mwinResult Run(mwinContext* context)
{
    return mwinRunLoop(context, Pump);
}

const mwinBackendOps mwinTestBackend = {
    Start, Stop, Run, CreateWindow, DestroyWindow, Submit, Now,
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

mwinResult mwinTestPost(mwinContext* context, const mwinEvent* event)
{
    if (context == nullptr || event == nullptr || event->type == mwin_eventNone ||
        event->type == mwin_eventWindowCreated || event->type == mwin_eventWindowDestroyed ||
        event->type >= mwin_eventRequestCompleted)
    {
        return mwin_errorInvalid;
    }
    TestPlatform* platform = PlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (mwinFindWindow(context, event->window) == nullptr)
    {
        return mwin_errorStale;
    }
    mwinEvent record = *event;
    record.timeNs = platform->timeNs;
    mwinPost(context, event->window.index1 - 1, &record);
    return mwin_success;
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
