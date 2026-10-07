// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The list of paths drops and dialogs deliver, and the file URIs it is
// often read from, on every host: a list filled to its bounds exactly,
// a block that grows only when a path does not fit, paths in UTF-16 as
// Windows hands them, and %XX escapes of every kind of hex digit. Paths
// and URIs sit alone in heap allocations of their length, and the
// allocator gives exact blocks, so AddressSanitizer sees a read past
// any of them. A whole text/uri-list goes into a drop: comments and
// empty lines are skipped, other schemes truncate it; and the rest of
// a drop as it is gathered.

#include "file_list.h"
#include "test_program.h"
#include "uri_list.h"

#include <stdlib.h>
#include <string.h>

static int s_allocations = 0;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_allocations += 1;
    return malloc(size);
}

static void Release(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static const mwinAllocator s_allocator = {Allocate, Release, nullptr};

static bool Holds(const mwinFileList* list, const char* bytes, uint32_t length)
{
    return list->length == length && memcmp(list->bytes, bytes, length) == 0;
}

static void TestBounds(void)
{
    mwinFileList list = {0};
    mwinListBounds bounds = {&s_allocator, 2, 6};
    CHECK(mwinListAdd(&list, bounds, "ab", 2) == mwin_listAdded &&
              mwinListAdd(&list, bounds, "cd", 2) == mwin_listAdded && Holds(&list, "ab\0cd", 6),
          "two paths fill the bytes exactly");
    CHECK(mwinListAdd(&list, bounds, "e", 1) == mwin_listFull, "a third is past the count");
    mwinListRelease(&list, &s_allocator);
    bounds.count = 3;
    CHECK(mwinListAdd(&list, bounds, "ab", 2) == mwin_listAdded &&
              mwinListAdd(&list, bounds, "cde", 3) == mwin_listFull && list.count == 1,
          "a path past the bytes");
    mwinListRelease(&list, &s_allocator);
    CHECK(mwinListAdd(&list, bounds, "", 0) == mwin_listNotPath &&
              mwinListAdd(&list, bounds, "a\0b", 3) == mwin_listNotPath &&
              mwinListAdd(&list, bounds, "\xFF", 1) == mwin_listNotPath && list.count == 0,
          "empty, holding a NUL, or not UTF-8");
}

static void TestGrowth(void)
{
    mwinFileList list = {0};
    mwinListBounds bounds = {&s_allocator, 10, 100};
    s_allocations = 0;
    CHECK(mwinListAdd(&list, bounds, "abcd", 4) == mwin_listAdded && list.capacity == 5,
          "the first block fits the first path");
    CHECK(mwinListAdd(&list, bounds, "x", 1) == mwin_listAdded && list.capacity == 10,
          "then it doubles");
    CHECK(mwinListAdd(&list, bounds, "xy", 2) == mwin_listAdded && list.capacity == 10 &&
              s_allocations == 2 && Holds(&list, "abcd\0x\0xy", 10),
          "a path that fills it exactly takes no new block");
    mwinListRelease(&list, &s_allocator);
}

// Adds UTF-16 held alone in a heap allocation of its length.
static mwinListResult AddUtf16(mwinFileList* list, mwinListBounds bounds, const uint16_t* path,
                               size_t length)
{
    uint16_t* copy = malloc(length > 0 ? length * sizeof(uint16_t) : 1);
    if (copy == nullptr)
    {
        return mwin_listFull;
    }
    if (length > 0)
    {
        memcpy(copy, path, length * sizeof(uint16_t));
    }
    mwinListResult result = mwinListAddUtf16(list, bounds, copy, length);
    free(copy);
    return result;
}

static void TestUtf16(void)
{
    mwinFileList list = {0};
    mwinListBounds bounds = {&s_allocator, 4, 64};
    static const uint16_t path[] = {'C', ':', '\\', 0xE9};
    static const uint16_t nul[] = {'a', 0, 'b'};
    static const uint16_t lone[] = {'a', 0xD800};
    CHECK(AddUtf16(&list, bounds, path, 4) == mwin_listAdded && Holds(&list, "C:\\\xC3\xA9", 6) &&
              list.count == 1,
          "a UTF-16 path, converted");
    CHECK(AddUtf16(&list, bounds, path, 0) == mwin_listNotPath &&
              AddUtf16(&list, bounds, nul, 3) == mwin_listNotPath &&
              AddUtf16(&list, bounds, lone, 2) == mwin_listNotPath && list.count == 1,
          "empty, holding a NUL, or a lone surrogate");
    bounds.bytes = 6;
    CHECK(AddUtf16(&list, bounds, path, 1) == mwin_listFull, "past the bytes");
    mwinListRelease(&list, &s_allocator);
}

// The path of a URI held alone in a heap allocation of its length, as
// a C string, or "" when it names no local file.
static bool PathIs(const char* uri, const char* expected)
{
    size_t length = strlen(uri);
    char* copy = malloc(length);
    if (copy == nullptr)
    {
        return false;
    }
    memcpy(copy, uri, length);
    char* path = nullptr;
    size_t pathLength = mwinFileUriPath(copy, length, &path);
    bool same = pathLength == strlen(expected) &&
                (pathLength == 0 || memcmp(path, expected, pathLength) == 0);
    free(copy);
    return same;
}

static void TestFileUris(void)
{
    CHECK(PathIs("file:///a%2fb%2F%09%41", "/a/b/\tA"), "escapes of every kind of digit");
    CHECK(PathIs("file://localhost/tmp/x", "/tmp/x"), "a local host");
    CHECK(PathIs("file:///a%4", "") && PathIs("file:///a%", "") && PathIs("file:///a%G1", ""),
          "an escape cut at the end or not hex");
    CHECK(PathIs("file://host/x", "") && PathIs("http://x/y", "") && PathIs("file://", ""),
          "another host, another scheme, no path");
}

// Gathers a list held alone in a heap allocation of its length.
static void Gather(mwinContext* context, const char* list)
{
    size_t length = strlen(list);
    char* copy = malloc(length);
    if (copy != nullptr)
    {
        memcpy(copy, list, length);
        mwinBeginDrop(context);
        mwinGatherUriList(context, copy, length);
        free(copy);
    }
}

static void GatherStep(Program* program, mwinContext* context, int step)
{
    (void)step;
    const mwinDropPayload* drop = &context->dropping;
    Gather(context, "# a comment\r\nfile:///a\r\n\r\nfile:///b%20c\n");
    CHECK(drop->files.count == 2 && Holds(&drop->files, "/a\0/b c", 8) && !drop->truncated,
          "files, past a comment and an empty line");
    Gather(context, "http://x/y\nfile:///d");
    CHECK(drop->files.count == 1 && Holds(&drop->files, "/d", 3) && drop->truncated,
          "another scheme truncates, the last line needs no end");
    // Windows hands paths and text in UTF-16; text set twice keeps the
    // second, the first given back.
    static const uint16_t path[] = {'C', ':', '\\', 0xE9};
    static const uint16_t text[] = {'h', 0xE9};
    mwinBeginDrop(context);
    mwinAddDroppedFileUtf16(context, path, 4);
    mwinSetDroppedTextUtf16(context, text, 2);
    CHECK(drop->files.count == 1 && Holds(&drop->files, "C:\\\xC3\xA9", 6) &&
              drop->textLength == 3 && memcmp(drop->text, "h\xC3\xA9", 3) == 0 && !drop->truncated,
          "a UTF-16 path and text");
    mwinSetDroppedText(context, "ab", 2);
    mwinSetDroppedText(context, "cde", 3);
    CHECK(drop->textLength == 3 && memcmp(drop->text, "cde", 3) == 0 && !drop->truncated,
          "the later text");
    mwinBeginDrop(context);
    program->done = true;
}

static void TestUriList(void)
{
    Program program = {.step = GatherStep};
    CHECK(Run(&program) == mwin_success, "the program runs");
}

int main(void)
{
    TestBounds();
    TestGrowth();
    TestUtf16();
    TestFileUris();
    TestUriList();
    return s_failures == 0 ? 0 : 1;
}
