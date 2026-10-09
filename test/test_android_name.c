// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The names of Android's document copies (android_name.h), on any
// platform: a name kept as it is, in UTF-8; '/' made '_', so that no name
// leaves its folder; "Document" for none, an empty name, "." and "..";
// a name of exactly the limit kept whole, a longer one cut to whole
// characters within it, never half a surrogate pair.

#include "android_name.h"
#include "test_harness.h"

#include <string.h>

#define LIMIT MWIN_ANDROID_NAME_BYTES

static char s_name[LIMIT + 1];

// The name of ASCII text.
static const char* NameOfText(const char* text)
{
    uint16_t units[64];
    size_t count = strlen(text);
    for (size_t i = 0; i < count; i++)
    {
        units[i] = (uint16_t)(unsigned char)text[i];
    }
    mwinAndroidNameOf(units, count, s_name);
    return s_name;
}

// The name of a unit repeated, or a pair when low is not 0.
static const char* NameOfRepeated(uint16_t unit, uint16_t low, size_t times)
{
    static uint16_t units[2 * (LIMIT + 2)];
    size_t count = 0;
    for (size_t i = 0; i < times; i++)
    {
        units[count++] = unit;
        if (low != 0)
        {
            units[count++] = low;
        }
    }
    mwinAndroidNameOf(units, count, s_name);
    return s_name;
}

// Whether a name is a byte sequence repeated a number of times.
static bool Repeats(const char* name, const char* sequence, size_t times)
{
    size_t length = strlen(sequence);
    if (strlen(name) != length * times)
    {
        return false;
    }
    for (size_t i = 0; i < times; i++)
    {
        if (memcmp(name + i * length, sequence, length) != 0)
        {
            return false;
        }
    }
    return true;
}

static void TestNames(void)
{
    CHECK(strcmp(NameOfText("report.txt"), "report.txt") == 0, "a name kept as it is");
    static const uint16_t accented[] = {'c', 0xE9, '.', 'm', 'd'};
    mwinAndroidNameOf(accented, 5, s_name);
    CHECK(strcmp(s_name, "c\xC3\xA9.md") == 0, "in UTF-8");
    CHECK(strcmp(NameOfText("a/b/../c"), "a_b_.._c") == 0 &&
              strcmp(NameOfText("../etc"), ".._etc") == 0 && strcmp(NameOfText("/"), "_") == 0,
          "every '/' made '_'");
    CHECK(strcmp(NameOfText("."), "Document") == 0 && strcmp(NameOfText(".."), "Document") == 0 &&
              strcmp(NameOfText("..."), "...") == 0,
          "\".\" and \"..\" not names; \"...\" one");
    mwinAndroidNameOf(nullptr, 3, s_name);
    bool none = strcmp(s_name, "Document") == 0;
    CHECK(none && strcmp(NameOfText(""), "Document") == 0, "\"Document\" for none or empty");
}

static void TestLimit(void)
{
    CHECK(Repeats(NameOfRepeated('x', 0, LIMIT), "x", LIMIT), "a name of the limit kept whole");
    CHECK(Repeats(NameOfRepeated('x', 0, LIMIT + 45), "x", LIMIT), "a longer one cut to the limit");
    // é is two bytes: 128 of them are 256, cut to 127.
    CHECK(Repeats(NameOfRepeated(0xE9, 0, 128), "\xC3\xA9", 127), "cut to whole characters");
    // U+10FFFF, DBFF DFFF, is four bytes: 64 are 256, cut to 63 pairs,
    // where half a pair would leave a replacement's three bytes.
    CHECK(Repeats(NameOfRepeated(0xDBFF, 0xDFFF, 64), "\xF4\x8F\xBF\xBF", 63),
          "never half a surrogate pair");
}

int main(void)
{
    TestNames();
    TestLimit();
    return s_failures == 0 ? 0 : 1;
}
