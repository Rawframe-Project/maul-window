// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the preferred locales the environment names: the input is
// LANGUAGE, a NUL, the messages locale, and the capacity in its first
// byte. The list must fit the capacity and hold tags of letters, digits
// and hyphens, separated by single commas, none twice.

#include "linux_locale.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

static bool IsTagByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

static void CheckList(const char* list, size_t length)
{
    size_t start = 0;
    while (start < length)
    {
        const char* comma = memchr(list + start, ',', length - start);
        size_t end = comma != nullptr ? (size_t)(comma - list) : length;
        Expect(end > start && (comma == nullptr || end + 1 < length));
        for (size_t i = start; i < end; i++)
        {
            Expect(IsTagByte(list[i]));
        }
        // No tag before this one is the same.
        for (size_t other = 0; other < start;)
        {
            const char* next = memchr(list + other, ',', start - other);
            size_t otherEnd = next != nullptr ? (size_t)(next - list) : start;
            Expect(otherEnd - other != end - start ||
                   memcmp(list + other, list + start, end - start) != 0);
            other = otherEnd + 1;
        }
        start = end + 1;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0)
    {
        return 0;
    }
    size_t capacity = data[0];
    char* text = malloc(size + 1);
    Expect(text != nullptr);
    memcpy(text, data + 1, size - 1);
    text[size - 1] = '\0';
    const char* language = text;
    size_t first = strlen(text);
    const char* messages = first + 1 < size ? text + first + 1 : nullptr;
    char* out = malloc(capacity + 1);
    Expect(out != nullptr);
    size_t length = mwinLinuxLocalesOf(language, messages, out, capacity);
    Expect(length <= capacity);
    CheckList(out, length);
    free(out);
    free(text);
    return 0;
}
