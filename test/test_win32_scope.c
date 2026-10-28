// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The input scope each purpose sets on Windows (win32_scope.h), on any
// platform: number, email, password and URL each their own, text and a
// value the contract does not name the default. On Windows, each scope
// must be inputscope.h's own number.

#include "test_harness.h"
#include "win32_scope.h"

#ifdef _WIN32
#include <inputscope.h>
#include <windows.h>
#endif

static void TestPurposes(void)
{
    CHECK(mwinWin32ScopeOf(mwin_purposeText) == mwin_scopeDefault, "text takes the default");
    CHECK(mwinWin32ScopeOf(mwin_purposeNumber) == mwin_scopeNumber, "number takes the number");
    CHECK(mwinWin32ScopeOf(mwin_purposeEmail) == mwin_scopeEmail, "email takes the email address");
    CHECK(mwinWin32ScopeOf(mwin_purposePassword) == mwin_scopePassword,
          "password takes the password");
    CHECK(mwinWin32ScopeOf(mwin_purposeUrl) == mwin_scopeUrl, "URL takes the URL");
    CHECK(mwinWin32ScopeOf(5) == mwin_scopeDefault && mwinWin32ScopeOf(255) == mwin_scopeDefault,
          "a value not named takes the default");
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
