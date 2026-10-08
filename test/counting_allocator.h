// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An allocator that counts the bytes it has given out and not had back,
// for tests that check a context lets go of what it took.

#ifndef MAUL_WINDOW_TEST_COUNTING_ALLOCATOR_H
#define MAUL_WINDOW_TEST_COUNTING_ALLOCATOR_H

#include "maul-window/base.h"

#include <stddef.h>
#include <stdlib.h>

static size_t s_countedBytes;

static inline void* CountedAllocate(size_t size, size_t alignment, void* context)
{
    (void)context;
    // aligned_alloc takes no alignment under a pointer's.
    void* memory = alignment <= alignof(max_align_t)
                       ? malloc(size)
                       : aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
    s_countedBytes += memory != nullptr ? size : 0;
    return memory;
}

static inline void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    if (memory != nullptr)
    {
        s_countedBytes -= size;
        free(memory);
    }
}

static inline mwinAllocator CountingAllocator(void)
{
    return (mwinAllocator){CountedAllocate, CountedFree, nullptr};
}

#endif // MAUL_WINDOW_TEST_COUNTING_ALLOCATOR_H
