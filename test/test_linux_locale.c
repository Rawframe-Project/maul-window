// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The preferred locales the environment names, as BCP 47 tags:
// - LANGUAGE in order, then the messages locale, without repeats;
// - LANGUAGE unheeded under the C locale or none;
// - codesets dropped; @latin, @cyrillic and @valencia kept as script
//   and variant, other modifiers dropped; UN M.49 territories kept;
// - names of no language left out;
// - tags that do not fit left out whole.

#include "linux_locale.h"
#include "test_harness.h"

#include <string.h>

static bool Gives(const char* language, const char* messages, size_t capacity, const char* expected)
{
    char out[64];
    memset(out, '#', sizeof(out));
    size_t length = mwinLinuxLocalesOf(language, messages, out, capacity);
    return length == strlen(expected) && memcmp(out, expected, length) == 0 &&
           (capacity == sizeof(out) || out[capacity] == '#');
}

int main(void)
{
    CHECK(Gives("de_DE:de:en", "de_DE.UTF-8", 64, "de-DE,de,en"),
          "LANGUAGE in order, then the messages locale, without repeats");
    CHECK(Gives(nullptr, "pt_BR.UTF-8", 64, "pt-BR") && Gives("", "fr_FR", 64, "fr-FR"),
          "the messages locale alone");
    CHECK(Gives("de:en", "C", 64, "") && Gives("de:en", "C.UTF-8", 64, "") &&
              Gives("de:en", "POSIX", 64, "") && Gives("de:en", nullptr, 64, "") &&
              Gives("de:en", "", 64, ""),
          "LANGUAGE unheeded under the C locale or none");
    CHECK(Gives(nullptr, "sr_RS.UTF-8@latin", 64, "sr-Latn-RS") &&
              Gives(nullptr, "uz_UZ@cyrillic", 64, "uz-Cyrl-UZ") &&
              Gives(nullptr, "ca_ES.UTF-8@valencia", 64, "ca-ES-valencia") &&
              Gives(nullptr, "de_DE@euro", 64, "de-DE") &&
              Gives(nullptr, "sr@latin", 64, "sr-Latn"),
          "scripts and variants kept, other modifiers dropped");
    CHECK(Gives(nullptr, "es_419.UTF-8", 64, "es-419") && Gives(nullptr, "EN_gb", 64, "en-GB") &&
              Gives(nullptr, "fil_PH", 64, "fil-PH"),
          "UN M.49 territories, cases and three-letter languages");
    CHECK(Gives("x:e1:english:de_D:de_DEU:de_12::de", "de_DE", 64, "de,de-DE"),
          "names of no language left out");
    CHECK(Gives("de_DE:fr_FR:it", "en_US", 12, "de-DE,fr-FR") &&
              Gives("de_DE:it", "en_US", 4, "it") && Gives("de_DE:it", "en_US", 8, "de-DE,it") &&
              Gives("de_DE:it", "en_US", 1, "") && Gives("de_DE:it", "en_US", 7, "de-DE"),
          "tags that do not fit left out whole");
    return s_failures == 0 ? 0 : 1;
}
