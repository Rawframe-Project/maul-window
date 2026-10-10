// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "test_harness.h"

#include "maul-window/base.h"

#include <string.h>

static void TestVersionMatchesHeader(void)
{
    mwinVersion version = mwinGetVersion();
    CHECK(version.major == MWIN_VERSION_MAJOR, "major version");
    CHECK(version.minor == MWIN_VERSION_MINOR, "minor version");
    CHECK(version.patch == MWIN_VERSION_PATCH, "patch version");
}

static void TestResultNames(void)
{
    CHECK(strcmp(mwinResultName(mwin_success), "mwin_success") == 0, "success name");
    CHECK(strcmp(mwinResultName(mwin_errorInvalid), "mwin_errorInvalid") == 0, "invalid name");
    CHECK(strcmp(mwinResultName(mwin_errorCapacity), "mwin_errorCapacity") == 0, "capacity name");
    CHECK(strcmp(mwinResultName(mwin_errorVersion), "mwin_errorVersion") == 0, "version name");
    CHECK(strcmp(mwinResultName(12345), "unknown result") == 0, "unknown name");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    return s_failures == 0 ? 0 : 1;
}
