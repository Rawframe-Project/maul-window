// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The input scope each purpose sets on Windows (win32_scope.h), on any
// platform: number, email, password and URL each their own, text and a
// value that is not a purpose the default. Each purpose the contract
// counts (MWIN_INPUT_PURPOSES) has one row in the test's table, so a
// purpose added without its scope fails. On Windows, each scope must be
// inputscope.h's own number.

#include "test_harness.h"
#include "win32_scope.h"

#ifdef _WIN32
#include <inputscope.h>
#include <windows.h>
#endif

// Each purpose's scope, one row a purpose: a purpose the contract gains
// needs a row, or the check of the count fails.
static const struct
{
    mwinInputPurpose purpose;
    int scope;
} s_scopes[] = {
    {mwin_purposeText, mwin_scopeDefault}, {mwin_purposeNumber, mwin_scopeNumber},
    {mwin_purposeEmail, mwin_scopeEmail},  {mwin_purposePassword, mwin_scopePassword},
    {mwin_purposeUrl, mwin_scopeUrl},
};

#define ROWS (sizeof(s_scopes) / sizeof(s_scopes[0]))

static void TestPurposes(void)
{
    // Every purpose below the count has its own row.
    bool rowed[256] = {false};
    bool unique = true;
    for (size_t i = 0; i < ROWS; i++)
    {
        unique = unique && !rowed[s_scopes[i].purpose];
        rowed[s_scopes[i].purpose] = true;
    }
    bool every = ROWS == MWIN_INPUT_PURPOSES && unique;
    for (int purpose = 0; purpose < MWIN_INPUT_PURPOSES; purpose++)
    {
        every = every && rowed[purpose];
    }
    CHECK(every, "a row for every purpose, and one each");
    for (size_t i = 0; i < ROWS; i++)
    {
        CHECK(mwinWin32ScopeOf(s_scopes[i].purpose) == s_scopes[i].scope,
              "each purpose takes its scope");
    }
    CHECK(mwinWin32ScopeOf(MWIN_INPUT_PURPOSES) == mwin_scopeDefault &&
              mwinWin32ScopeOf(255) == mwin_scopeDefault,
          "a value not a purpose takes the default");
}

static void TestWindowsNumbers(void)
{
#ifdef _WIN32
    CHECK(mwin_scopeDefault == IS_DEFAULT && mwin_scopeUrl == IS_URL &&
              mwin_scopeEmail == IS_EMAIL_SMTPEMAILADDRESS && mwin_scopeNumber == IS_NUMBER &&
              mwin_scopePassword == IS_PASSWORD,
          "inputscope.h's numbers");
#endif
}

int main(void)
{
    TestPurposes();
    TestWindowsNumbers();
    return s_failures == 0 ? 0 : 1;
}
