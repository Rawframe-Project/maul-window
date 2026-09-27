// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The event stream: each window's records wait in its own ring, and the
// stream hands out the oldest record of all rings by a sequence number
// the context counts. A state notification replaces one of its class
// still waiting for the same window (the newer one moves to the end, so
// order stays true), which bounds the waiting notifications of a window
// by the number of classes plus its requests in flight; the context
// refuses limits below that bound.

#include "core.h"

// The class of a notification that a newer one of the same class
// replaces, or 0 for records that never coalesce.
static int CoalesceClass(mwinEventType type)
{
    switch (type)
    {
    case mwin_eventResized:
        return 1;
    case mwin_eventPixelSizeChanged:
        return 2;
    case mwin_eventScaleChanged:
        return 3;
    case mwin_eventMoved:
        return 4;
    case mwin_eventModeChanged:
        return 5;
    case mwin_eventFocusGained:
    case mwin_eventFocusLost:
        return 6;
    case mwin_eventOccluded:
    case mwin_eventRevealed:
        return 7;
    case mwin_eventShown:
    case mwin_eventHidden:
        return 8;
    case mwin_eventCloseRequested:
        return 9;
    default:
        return 0;
    }
}

static uint16_t At(const mwinRing* ring, uint16_t index)
{
    return (uint16_t)((ring->head + index) % ring->capacity);
}

// Removes the record at position index, keeping the others in order.
static void RemoveAt(mwinRing* ring, uint16_t index)
{
    for (uint16_t i = index; i + 1 < ring->count; i++)
    {
        ring->events[At(ring, i)] = ring->events[At(ring, (uint16_t)(i + 1))];
        ring->sequences[At(ring, i)] = ring->sequences[At(ring, (uint16_t)(i + 1))];
    }
    ring->count -= 1;
}

static void Append(mwinContext* context, mwinRing* ring, const mwinEvent* event)
{
    int coalesce = CoalesceClass(event->type);
    for (uint16_t i = 0; coalesce != 0 && i < ring->count; i++)
    {
        if (CoalesceClass(ring->events[At(ring, i)].type) == coalesce)
        {
            RemoveAt(ring, i);
            break;
        }
    }
    if (ring->count == ring->capacity)
    {
        return; // unreachable while the context keeps its limits' bound
    }
    uint16_t slot = At(ring, ring->count);
    ring->events[slot] = *event;
    ring->sequences[slot] = context->sequence++;
    ring->count += 1;
}

// The window state a notification reports.
static void Apply(mwinWindowState* state, const mwinEvent* event)
{
    switch (event->type)
    {
    case mwin_eventWindowCreated:
        state->created = true;
        break;
    case mwin_eventResized:
        state->size = event->data.size;
        break;
    case mwin_eventPixelSizeChanged:
        state->pixelSize = event->data.pixelSize;
        break;
    case mwin_eventScaleChanged:
        state->scale = event->data.scale.scale;
        break;
    case mwin_eventMoved:
        state->position = event->data.position;
        break;
    case mwin_eventModeChanged:
        state->mode = event->data.mode;
        break;
    case mwin_eventFocusGained:
    case mwin_eventFocusLost:
        state->focused = event->type == mwin_eventFocusGained;
        break;
    case mwin_eventOccluded:
    case mwin_eventRevealed:
        state->occluded = event->type == mwin_eventOccluded;
        break;
    case mwin_eventShown:
    case mwin_eventHidden:
        state->visible = event->type == mwin_eventShown;
        break;
    default:
        break;
    }
}

void mwinPost(mwinContext* context, uint32_t slot, const mwinEvent* event)
{
    mwinWindow* window = &context->windows[slot];
    if (window->status != mwin_slotLive)
    {
        return;
    }
    mwinEvent record = *event;
    record.window = mwinWindowIdOf(context, slot);
    Apply(&window->state, &record);
    Append(context, &window->ring, &record);
}

void mwinPostDestroyed(mwinContext* context, uint32_t slot, uint64_t timeNs)
{
    // Completions stay: every request is answered, even a cancelled one.
    mwinRing* ring = &context->windows[slot].ring;
    for (uint16_t i = ring->count; i > 0; i--)
    {
        if (ring->events[At(ring, (uint16_t)(i - 1))].type != mwin_eventRequestCompleted)
        {
            RemoveAt(ring, (uint16_t)(i - 1));
        }
    }
    mwinEvent event = {0};
    event.type = mwin_eventWindowDestroyed;
    event.window = mwinWindowIdOf(context, slot);
    event.timeNs = timeNs;
    Append(context, ring, &event);
}

mwinResult mwinNextEvent(mwinContext* context, mwinEvent* eventOut)
{
    if (context == nullptr || eventOut == nullptr)
    {
        return mwin_errorInvalid;
    }
    mwinWindow* oldest = nullptr;
    uint64_t oldestSequence = UINT64_MAX;
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        mwinRing* ring = &context->windows[i].ring;
        if (ring->count > 0 && ring->sequences[ring->head] < oldestSequence)
        {
            oldest = &context->windows[i];
            oldestSequence = ring->sequences[ring->head];
        }
    }
    if (oldest == nullptr)
    {
        return mwin_empty;
    }
    mwinRing* ring = &oldest->ring;
    *eventOut = ring->events[ring->head];
    ring->head = (uint16_t)((ring->head + 1) % ring->capacity);
    ring->count -= 1;
    if (oldest->status == mwin_slotDestroyed && eventOut->type == mwin_eventWindowDestroyed)
    {
        oldest->status = mwin_slotFree;
    }
    if (eventOut->type == mwin_eventRequestCompleted)
    {
        mwinReleaseRequest(context, eventOut->data.completion.request);
    }
    return mwin_success;
}
