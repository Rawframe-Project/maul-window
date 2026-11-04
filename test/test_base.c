// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version, result names, and the result attribute.

#include "test_harness.h"

#include "maul-window/base.h"

#include <string.h>

// The suites build as C23, where every compiler the family takes has
// [[nodiscard]], so MWIN_NODISCARD expands to it and a dropped status
// warns (GCC 14 reports its C23 as 202000L).
#define TEXT(x)     #x
#define EXPANDED(x) TEXT(x)
static_assert(sizeof(EXPANDED(MWIN_NODISCARD)) > 1, "MWIN_NODISCARD is the attribute in C23");

static void TestVersionMatchesHeader(void)
{
    mwinVersion version = mwinGetVersion();
    CHECK(version.major == MWIN_VERSION_MAJOR, "major version");
    CHECK(version.minor == MWIN_VERSION_MINOR, "minor version");
    CHECK(version.patch == MWIN_VERSION_PATCH, "patch version");
}

static void TestResultNames(void)
{
    CHECK(strcmp(mwinGetResultName(mwin_success), "mwin_success") == 0, "success name");
    CHECK(strcmp(mwinGetResultName(mwin_errorInvalid), "mwin_errorInvalid") == 0, "invalid name");
    CHECK(strcmp(mwinGetResultName(mwin_errorCapacity), "mwin_errorCapacity") == 0,
          "capacity name");
    CHECK(strcmp(mwinGetResultName(mwin_errorVersion), "mwin_errorVersion") == 0, "version name");
    CHECK(strcmp(mwinGetResultName(12345), "unknown result") == 0, "unknown name");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    return s_failures == 0 ? 0 : 1;
}
