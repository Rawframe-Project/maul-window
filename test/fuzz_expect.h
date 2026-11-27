// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A fuzz target's promises. When one breaks, a single line names it and
// the run stops: the "FILE:LINE: expectation failed: ..." line that
// tools/fuzz_target.sh keeps in a finding's report, since the input
// itself stays on the machine that ran it. The line holds the promise's
// own text and, for a comparison, the two values, never the input's
// bytes. The fuzz targets are built with the source folder taken out of
// __FILE__, so FILE is the path in the repository.

#ifndef MAUL_WINDOW_TEST_FUZZ_EXPECT_H
#define MAUL_WINDOW_TEST_FUZZ_EXPECT_H

#include <stdio.h>
#include <stdlib.h>

[[noreturn]] static inline void mwinFuzzBroken(const char* file, int line, const char* promise)
{
    (void)fprintf(stderr, "%s:%d: expectation failed: %s\n", file, line, promise);
    (void)fflush(stderr);
    abort();
}

static inline void mwinFuzzExpect(bool condition, const char* file, int line, const char* promise)
{
    if (!condition)
    {
        mwinFuzzBroken(file, line, promise);
    }
}

static inline void mwinFuzzExpectEqual(long long value, long long expected, const char* file,
                                       int line, const char* valueText, const char* expectedText)
{
    if (value != expected)
    {
        (void)fprintf(stderr, "%s:%d: expectation failed: %s == %s, %s is %lld, %s is %lld\n",
                      file, line, valueText, expectedText, valueText, value, expectedText,
                      expected);
        (void)fflush(stderr);
        abort();
    }
}

// The promise that condition holds.
#define Expect(condition) mwinFuzzExpect((condition), __FILE__, __LINE__, #condition)

// The promise that an integer or enumeration value, such as a status,
// equals the one expected; a broken one prints both.
#define ExpectEqual(value, expected)                                                               \
    mwinFuzzExpectEqual((long long)(value), (long long)(expected), __FILE__, __LINE__, #value,     \
                        #expected)

#endif
