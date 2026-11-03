// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Windows' preferred languages handed to the context (win32_locales.h),
// on any platform, on the test backend with a localeBytes limit of 4.
// Windows reads its whole list into the limit, or, for a longer one, its
// first language into the limit and one more byte:
// - A first language one byte past the limit leaves the list empty, not
//   as it was.
// - A list that fits is taken; one a byte past the limit keeps its
//   leading tags that fit.
// The text is a block of exactly the limit and one more byte, where
// AddressSanitizer would see a write past it. Not exercised: reading
// the list from Windows (test_win32_system).

#include "test_harness.h"
#include "test_program.h"
#include "win32_locales.h"

#include "maul-window/system.h"

#include <stdlib.h>
#include <string.h>

#define LIMIT 4

static bool LocalesAre(mwinContext* context, const char* expected)
{
    char locales[16];
    size_t length = 0;
    return mwinContext_GetPreferredLocales(context, locales, sizeof(locales), &length) ==
               mwin_success &&
           length == strlen(expected) && memcmp(locales, expected, length) == 0;
}

// Hands the context ASCII names as Windows' UTF-16 units.
static void Take(mwinContext* context, char* text, const char* names)
{
    uint16_t units[16];
    size_t length = strlen(names);
    for (size_t i = 0; i < length; i++)
    {
        units[i] = (uint16_t)(unsigned char)names[i];
    }
    mwinWin32TakeLocales(context, units, length, text, 0);
}

static void Step(Program* program, mwinContext* context, int step)
{
    (void)step;
    char* text = malloc(LIMIT + 1);
    if (text == nullptr)
    {
        CHECK(false, "the text's block allocated");
        program->done = true;
        return;
    }
    CHECK(mwinSetLocales(context, "nl", 2, 0) && LocalesAre(context, "nl"),
          "the list starts as nl");
    Take(context, text, "de-DE");
    CHECK(LocalesAre(context, ""), "a first language a byte past the limit leaves the list empty");
    Take(context, text, "fr");
    CHECK(LocalesAre(context, "fr"), "a list that fits taken");
    Take(context, text, "de,fr");
    CHECK(LocalesAre(context, "de"),
          "a list a byte past the limit keeps its leading tags that fit");
    free(text);
    program->done = true;
}

int main(void)
{
    Program program = {.step = Step};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.localeBytes = LIMIT;
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
