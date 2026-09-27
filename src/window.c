// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Windows and their requests. A request takes a free slot of its
// window, supersedes an active one of the same kind, and goes to the
// backend; mwinComplete answers it with a completion record, and the
// slot is free again once the program drains that record.

#include "maul-window/window.h"

#include "core.h"

#include "maul-unicode/encoding.h"

#include <math.h>
#include <string.h>

#define WINDOW_DEF_COOKIE 0x6D77696Eu

mwinWindowDef mwinDefaultWindowDef(void)
{
    mwinWindowDef def = {0};
    def.cookie = WINDOW_DEF_COOKIE;
    def.size = (mwinSize){1280.0f, 720.0f};
    def.mode = mwin_modeWindowed;
    def.visible = true;
    def.resizable = true;
    def.decorated = true;
    return def;
}

mwinWindowId mwinWindowIdOf(const mwinContext* context, uint32_t slot)
{
    return (mwinWindowId){slot + 1, context->windows[slot].generation};
}

mwinWindow* mwinFindWindow(const mwinContext* context, mwinWindowId window)
{
    if (window.index1 == 0 || window.index1 > context->limits.windows)
    {
        return nullptr;
    }
    mwinWindow* found = &context->windows[window.index1 - 1];
    return found->status == mwin_slotLive && found->generation == window.generation ? found
                                                                                    : nullptr;
}

static bool IsText(const char* text, size_t length, uint16_t limit)
{
    return (text != nullptr || length == 0) && length <= limit &&
           muniValidateUtf8(text, length).status == muni_success;
}

static bool IsPositive(mwinSize size)
{
    return isfinite(size.width) && isfinite(size.height) && size.width > 0.0f && size.height > 0.0f;
}

static mwinRequestId RequestIdOf(const mwinContext* context, uint32_t slot, uint32_t request)
{
    const mwinWindow* window = &context->windows[slot];
    return (mwinRequestId){slot * context->limits.requestsPerWindow + request + 1,
                           window->requests[request].generation};
}

void mwinComplete(mwinContext* context, uint32_t slot, uint32_t request, mwinOutcome outcome)
{
    mwinRequest* entry = &context->windows[slot].requests[request];
    if (entry->status != mwin_requestActive)
    {
        return;
    }
    entry->status = mwin_requestAnswered;
    if (entry->kind == mwin_requestTextInput && outcome == mwin_outcomeDone)
    {
        context->windows[slot].state.textInput = entry->value.textInput.enabled;
    }
    mwinEvent event = {0};
    event.type = mwin_eventRequestCompleted;
    event.timeNs = context->backend->now(context);
    event.data.completion.request = RequestIdOf(context, slot, request);
    event.data.completion.kind = entry->kind;
    event.data.completion.outcome = outcome;
    mwinPost(context, slot, &event);
}

void mwinReleaseRequest(mwinContext* context, mwinRequestId request)
{
    uint32_t perWindow = context->limits.requestsPerWindow;
    uint32_t slot = (request.index1 - 1) / perWindow;
    mwinRequest* entry = &context->windows[slot].requests[(request.index1 - 1) % perWindow];
    if (entry->status == mwin_requestAnswered && entry->generation == request.generation)
    {
        entry->status = mwin_requestFree;
        entry->generation += 1;
    }
}

int32_t mwinFindActiveRequest(const mwinWindow* window, uint16_t count, mwinRequestKind kind)
{
    for (uint16_t i = 0; i < count; i++)
    {
        if (window->requests[i].status == mwin_requestActive && window->requests[i].kind == kind)
        {
            return i;
        }
    }
    return -1;
}

// Takes a request slot of the window for a kind, superseding an active
// request of that kind; -1 when every slot is taken.
static int32_t TakeRequest(mwinContext* context, uint32_t slot, mwinRequestKind kind)
{
    mwinWindow* window = &context->windows[slot];
    uint16_t count = context->limits.requestsPerWindow;
    int32_t free = -1;
    for (uint16_t i = 0; i < count && free < 0; i++)
    {
        free = window->requests[i].status == mwin_requestFree ? i : -1;
    }
    int32_t older = mwinFindActiveRequest(window, count, kind);
    if (free < 0)
    {
        return -1;
    }
    if (older >= 0)
    {
        mwinComplete(context, slot, (uint32_t)older, mwin_outcomeSuperseded);
    }
    window->requests[free].status = mwin_requestActive;
    window->requests[free].kind = kind;
    return free;
}

void mwinSubmitRequest(mwinContext* context, uint32_t slot, int32_t request,
                       mwinRequestId* requestOut)
{
    if (requestOut != nullptr)
    {
        *requestOut = RequestIdOf(context, slot, (uint32_t)request);
    }
    context->backend->submit(context, slot, (uint32_t)request);
}

static bool IsDefValid(const mwinContext* context, const mwinWindowDef* def)
{
    return def->cookie == WINDOW_DEF_COOKIE && IsPositive(def->size) &&
           def->mode <= mwin_modeMaximized &&
           IsText(def->title, def->titleLength, context->limits.titleBytes);
}

mwinResult mwinCreateWindow(mwinContext* context, const mwinWindowDef* def, mwinWindowId* windowOut,
                            mwinRequestId* requestOut)
{
    if (context == nullptr || def == nullptr || windowOut == nullptr || !IsDefValid(context, def))
    {
        return mwin_errorInvalid;
    }
    uint32_t slot = 0;
    while (slot < context->limits.windows && context->windows[slot].status != mwin_slotFree)
    {
        slot += 1;
    }
    if (slot == context->limits.windows)
    {
        return mwin_errorCapacity;
    }
    mwinWindow* window = &context->windows[slot];
    window->status = mwin_slotLive;
    window->generation += 1;
    window->state = (mwinWindowState){0};
    window->def = *def;
    window->def.title = nullptr;
    window->titleLength = (uint16_t)def->titleLength;
    if (def->titleLength > 0)
    {
        memcpy(window->title, def->title, def->titleLength);
    }
    int32_t request = TakeRequest(context, slot, mwin_requestCreate);
    *windowOut = mwinWindowIdOf(context, slot);
    if (requestOut != nullptr)
    {
        *requestOut = RequestIdOf(context, slot, (uint32_t)request);
    }
    context->backend->createWindow(context, slot);
    return mwin_success;
}

mwinResult mwinDestroyWindow(mwinContext* context, mwinWindowId window)
{
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    mwinWindow* found = mwinFindWindow(context, window);
    if (found == nullptr)
    {
        return mwin_errorStale;
    }
    uint32_t slot = window.index1 - 1;
    for (uint32_t i = 0; i < context->limits.requestsPerWindow; i++)
    {
        mwinComplete(context, slot, i, mwin_outcomeCancelled);
    }
    context->backend->destroyWindow(context, slot);
    mwinPostDestroyed(context, slot, context->backend->now(context));
    found->status = mwin_slotDestroyed;
    return mwin_success;
}

mwinResult mwinGetWindowState(const mwinContext* context, mwinWindowId window,
                              mwinWindowState* stateOut)
{
    if (context == nullptr || stateOut == nullptr)
    {
        return mwin_errorInvalid;
    }
    const mwinWindow* found = mwinFindWindow(context, window);
    if (found == nullptr)
    {
        return mwin_errorStale;
    }
    *stateOut = found->state;
    return mwin_success;
}

mwinResult mwinBeginRequest(mwinContext* context, mwinWindowId window, mwinRequestKind kind,
                            uint32_t* slotOut, int32_t* requestOut)
{
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (mwinFindWindow(context, window) == nullptr)
    {
        return mwin_errorStale;
    }
    *slotOut = window.index1 - 1;
    *requestOut = TakeRequest(context, *slotOut, kind);
    return *requestOut < 0 ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinRequestTitle(mwinContext* context, mwinWindowId window, const char* title,
                            size_t length, mwinRequestId* requestOut)
{
    if (context != nullptr && !IsText(title, length, context->limits.titleBytes))
    {
        return length > context->limits.titleBytes ? mwin_errorCapacity : mwin_errorInvalid;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestTitle, &slot, &request);
    if (status != mwin_success)
    {
        return status;
    }
    mwinWindow* found = &context->windows[slot];
    if (length > 0)
    {
        memcpy(found->pendingTitle, title, length);
    }
    found->pendingTitleLength = (uint16_t)length;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

mwinResult mwinRequestSize(mwinContext* context, mwinWindowId window, mwinSize size,
                           mwinRequestId* requestOut)
{
    if (!IsPositive(size))
    {
        return mwin_errorInvalid;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestSize, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.size = size;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestPosition(mwinContext* context, mwinWindowId window, mwinPosition position,
                               mwinRequestId* requestOut)
{
    if (!isfinite(position.x) || !isfinite(position.y))
    {
        return mwin_errorInvalid;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestPosition, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.position = position;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestMode(mwinContext* context, mwinWindowId window, mwinWindowMode mode,
                           mwinRequestId* requestOut)
{
    if (mode > mwin_modeMaximized)
    {
        return mwin_errorInvalid;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestMode, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.mode = mode;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestVisible(mwinContext* context, mwinWindowId window, bool visible,
                              mwinRequestId* requestOut)
{
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestVisible, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.visible = visible;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestFocus(mwinContext* context, mwinWindowId window, mwinRequestId* requestOut)
{
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestFocus, &slot, &request);
    if (status == mwin_success)
    {
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}
