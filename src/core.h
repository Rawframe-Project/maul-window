// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context and what it owns, and the functions backends call to
// report what the platform did. Everything lives in one block from the
// def's allocator: the window slots, each with its record storage, its
// request slots and its two title buffers.
//
// A window slot is free, live, or destroyed with its
// mwin_eventWindowDestroyed record still waiting; it is free again once
// that record is drained, so a burst of destroyed windows can never
// outgrow the storage.

#ifndef MAUL_WINDOW_SRC_CORE_H
#define MAUL_WINDOW_SRC_CORE_H

#include "backend.h"

#include "maul-window/event.h"

// Records waiting in arrival order, in a circular buffer.
typedef struct mwinRing
{
    mwinEvent* events;
    uint64_t* sequences;
    uint16_t head;
    uint16_t count;
    uint16_t capacity;
} mwinRing;

// A request slot is free, active (the backend has it), or answered (its
// completion waits in the stream); it is free again once the completion
// is drained, so completions waiting never outnumber the slots.
enum
{
    mwin_requestFree = 0,
    mwin_requestActive = 1,
    mwin_requestAnswered = 2,
};

typedef struct mwinRequest
{
    uint32_t generation;
    uint8_t status;
    mwinRequestKind kind;
    union
    {
        mwinSize size;
        mwinPosition position;
        mwinWindowMode mode;
        bool visible;
    } value;
} mwinRequest;

enum
{
    mwin_slotFree = 0,
    mwin_slotLive = 1,
    mwin_slotDestroyed = 2,
};

typedef struct mwinWindow
{
    uint32_t generation;
    uint8_t status;
    mwinWindowState state;
    // What the program asked for at creation, for the backend.
    mwinWindowDef def;
    mwinRing ring;
    mwinRequest* requests;
    char* title;
    char* pendingTitle;
    uint16_t titleLength;
    uint16_t pendingTitleLength;
    // The backend's own data for the window.
    void* platform;
} mwinWindow;

struct mwinContext
{
    mwinAllocator allocator;
    size_t memorySize;
    mwinLimits limits;
    const mwinBackendOps* backend;
    void* backendData;
    const mwinAppDef* app;
    uint64_t sequence;
    mwinWindow* windows;
    // The program's functions are running; a critical frame waits.
    bool inProgram;
};

// The window a live id names, or NULL.
mwinWindow* mwinFindWindow(const mwinContext* context, mwinWindowId window);

// The id of the window in a slot.
mwinWindowId mwinWindowIdOf(const mwinContext* context, uint32_t slot);

// Reports a notification about the window in a slot: its state takes
// the record's values, and the record joins the stream. A newer
// notification of the same class replaces one still waiting.
void mwinPost(mwinContext* context, uint32_t slot, const mwinEvent* event);

// Queues the destroyed record of a slot whose window just went; its
// notifications still waiting go with it, its completions stay.
void mwinPostDestroyed(mwinContext* context, uint32_t slot, uint64_t timeNs);

// Answers a request of the window in a slot; a request that is no
// longer active (superseded, cancelled) is left alone.
void mwinComplete(mwinContext* context, uint32_t slot, uint32_t request, mwinOutcome outcome);

// Frees the slot of an answered request whose completion was drained.
void mwinReleaseRequest(mwinContext* context, mwinRequestId request);

// The request slot of an active request of a kind, or -1.
int32_t mwinFindActiveRequest(const mwinWindow* window, uint16_t count, mwinRequestKind kind);

#endif // MAUL_WINDOW_SRC_CORE_H
