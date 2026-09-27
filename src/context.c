// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context: its defs, its one block of memory, and mwinRun. The block
// holds the context, then the window slots, then per window its record
// ring, its request slots and its two title buffers.

#include "maul-window/context.h"

#include "allocator.h"
#include "core.h"

#include <stdalign.h>
#include <string.h>

#define CONTEXT_DEF_COOKIE 0x6D776378u
#define APP_DEF_COOKIE     0x6D776170u

// The notification classes that coalesce, plus created and destroyed:
// with a window's completions, the most records that can wait for it.
#define FIXED_RECORDS 11

mwinContextDef mwinDefaultContextDef(void)
{
    mwinContextDef def = {0};
    def.cookie = CONTEXT_DEF_COOKIE;
    def.limits.windows = 8;
    def.limits.requestsPerWindow = 32;
    def.limits.notificationsPerWindow = 256;
    def.limits.titleBytes = 1024;
    def.backend = mwin_backendNative;
    return def;
}

mwinAppDef mwinDefaultAppDef(void)
{
    mwinAppDef def = {0};
    def.cookie = APP_DEF_COOKIE;
    def.context = mwinDefaultContextDef();
    return def;
}

static bool IsDefValid(const mwinAppDef* def)
{
    const mwinContextDef* context = &def->context;
    const mwinLimits* limits = &context->limits;
    return def->cookie == APP_DEF_COOKIE && def->init != nullptr && def->frame != nullptr &&
           context->cookie == CONTEXT_DEF_COOKIE && mwinIsAllocatorValid(&context->allocator) &&
           limits->windows > 0 && limits->requestsPerWindow > 0 && limits->titleBytes > 0 &&
           limits->notificationsPerWindow >= limits->requestsPerWindow + FIXED_RECORDS &&
           context->backend <= mwin_backendTest;
}

static size_t RoundUp(size_t size)
{
    return (size + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1);
}

// The bytes one window slot's storage takes.
static size_t WindowBytes(const mwinLimits* limits)
{
    size_t records = limits->notificationsPerWindow;
    return RoundUp(records * sizeof(mwinEvent)) + RoundUp(records * sizeof(uint64_t)) +
           RoundUp(limits->requestsPerWindow * sizeof(mwinRequest)) +
           2 * RoundUp(limits->titleBytes);
}

// Points each window slot at its part of the block after the slots.
static void Lay(mwinContext* context, unsigned char* storage)
{
    const mwinLimits* limits = &context->limits;
    for (uint32_t i = 0; i < limits->windows; i++)
    {
        mwinWindow* window = &context->windows[i];
        size_t records = limits->notificationsPerWindow;
        window->ring.events = (mwinEvent*)storage;
        storage += RoundUp(records * sizeof(mwinEvent));
        window->ring.sequences = (uint64_t*)storage;
        storage += RoundUp(records * sizeof(uint64_t));
        window->ring.capacity = (uint16_t)records;
        window->requests = (mwinRequest*)storage;
        storage += RoundUp(limits->requestsPerWindow * sizeof(mwinRequest));
        window->title = (char*)storage;
        storage += RoundUp(limits->titleBytes);
        window->pendingTitle = (char*)storage;
        storage += RoundUp(limits->titleBytes);
    }
}

static const mwinBackendOps* FindBackend(mwinBackendKind kind)
{
#ifdef MAUL_WINDOW_TEST_BACKEND
    if (kind == mwin_backendTest)
    {
        return &mwinTestBackend;
    }
#endif
    (void)kind;
    return nullptr;
}

static mwinResult CreateContext(const mwinAppDef* def, mwinContext** contextOut)
{
    const mwinLimits* limits = &def->context.limits;
    size_t header = RoundUp(sizeof(mwinContext)) + RoundUp(limits->windows * sizeof(mwinWindow));
    size_t size = header + limits->windows * WindowBytes(limits);
    unsigned char* block = mwinAllocate(&def->context.allocator, size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, size);
    mwinContext* context = (mwinContext*)block;
    context->allocator = def->context.allocator;
    context->memorySize = size;
    context->limits = *limits;
    context->app = def;
    context->windows = (mwinWindow*)(block + RoundUp(sizeof(mwinContext)));
    Lay(context, block + header);
    *contextOut = context;
    return mwin_success;
}

static void DestroyContext(mwinContext* context)
{
    mwinAllocator allocator = context->allocator;
    mwinRelease(&allocator, context, context->memorySize, alignof(max_align_t));
}

mwinResult mwinRun(const mwinAppDef* def)
{
    if (def == nullptr || !IsDefValid(def))
    {
        return mwin_errorInvalid;
    }
    const mwinBackendOps* backend = FindBackend(def->context.backend);
    if (backend == nullptr)
    {
        return mwin_errorUnsupported;
    }
    mwinContext* context = nullptr;
    mwinResult status = CreateContext(def, &context);
    if (status != mwin_success)
    {
        return status;
    }
    context->backend = backend;
    status = backend->start(context);
    if (status == mwin_success)
    {
        status = backend->run(context);
        backend->stop(context);
    }
    DestroyContext(context);
    return status;
}

mwinResult mwinRunLoop(mwinContext* context, void (*pump)(mwinContext* context))
{
    const mwinAppDef* app = context->app;
    context->inProgram = true;
    mwinResult status = app->init(context, app->user);
    context->inProgram = false;
    while (status == mwin_success)
    {
        pump(context);
        context->inProgram = true;
        mwinFrameResult result = app->frame(context, app->user);
        context->inProgram = false;
        if (result != mwin_frameContinue)
        {
            break;
        }
    }
    if (app->quit != nullptr)
    {
        context->inProgram = true;
        app->quit(context, status, app->user);
        context->inProgram = false;
    }
    return status;
}
