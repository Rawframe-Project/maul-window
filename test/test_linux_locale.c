// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The preferred locales the environment names, as BCP 47 tags:
// - LANGUAGE in order, then the messages locale, without repeats;
// - LANGUAGE unheeded under the C locale or none;
// - codesets dropped; @latin, @cyrillic and @valencia kept as script
//   and variant, other modifiers dropped; UN M.49 territories kept;
// - names of no language left out;
// - tags that do not fit left out whole;
// - the messages locale taken from LC_ALL, else LC_MESSAGES, else LANG.
// plasma-localerc's [Translations] LANGUAGE:
// - found among other groups and keys, with blanks and CR LF line ends;
// - the last of several taken;
// - no language where the key is missing, empty or names none;
// - a text with a NUL refused.

#include "linux_locale.h"
#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

static bool Gives(const char* language, const char* messages, size_t capacity, const char* expected)
{
    char out[64];
    memset(out, '#', sizeof(out));
    size_t length = mwinLinuxLocalesOf(language, messages, out, capacity);
    return length == strlen(expected) && memcmp(out, expected, length) == 0 &&
           (capacity == sizeof(out) || out[capacity] == '#');
}

// What plasma-localerc's text gives: its tags, or "!" where it is refused.
static bool Plasma(const char* text, size_t length, size_t capacity, const char* expected)
{
    char out[64];
    memset(out, '#', sizeof(out));
    size_t got = 99;
    if (!mwinLinuxPlasmaLocales(text, length, out, capacity, &got))
    {
        return strcmp(expected, "!") == 0;
    }
    return got == strlen(expected) && memcmp(out, expected, got) == 0 && out[capacity] == '#';
}

#define PLASMA(text, capacity, expected) Plasma(text, sizeof(text) - 1, capacity, expected)

int main(void)
{
    CHECK(Gives("de_DE:de:en", "de_DE.UTF-8", 64, "de-DE,de,en"),
          "LANGUAGE in order, then the messages locale, without repeats");
    CHECK(Gives("de:de", "fr_FR", 64, "de,fr-FR") && Gives("fr:de", "de", 64, "fr,de"),
          "no repeat of a list's only tag, or of its last");
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
    CHECK(Gives(nullptr, "ZU_za", 64, "zu-ZA"), "cases at the end of the alphabet");
    CHECK(Gives("x:e1:english:de_D:de_DEU:de_12::de", "de_DE", 64, "de,de-DE"),
          "names of no language left out");
    CHECK(Gives("de_DE:fr_FR:it", "en_US", 12, "de-DE,fr-FR") &&
              Gives("de_DE:it", "en_US", 4, "it") && Gives("de_DE:it", "en_US", 8, "de-DE,it") &&
              Gives("de_DE:it", "en_US", 1, "") && Gives("de_DE:it", "en_US", 7, "de-DE") &&
              Gives("de_DE:it", "en_US", 5, "de-DE"),
          "tags that do not fit left out whole, a first that just fits kept");
    CHECK(PLASMA("[Formats]\nLANG=de_DE.UTF-8\nLANGUAGE=it\n\n[Translations]\nLANGUAGES=es\n"
                 "LANGUAGE=fr:en_GB:de\n[Other]\nLANGUAGE=pt\n",
                 32, "fr,en-GB,de"),
          "the key of its group only");
    CHECK(PLASMA("  [Translations] \r\n\tLANGUAGE \t= de_DE:en_US  \r\n", 32, "de-DE,en-US") &&
              PLASMA("[Translations]\nLANGUAGE=de", 32, "de"),
          "blanks, CR LF line ends and no last line end");
    CHECK(PLASMA("[Translations]\nLANGUAGE=de\nLANGUAGE=fr:it\n", 32, "fr,it"),
          "the last of several");
    CHECK(PLASMA("[Translations]\nLANGUAGE=\n", 32, "") &&
              PLASMA("[Translations]\nLANGUAGE=C:x1:\xff\xfe\n", 32, "") &&
              PLASMA("[Formats]\nLANG=de_DE\n", 32, "") && PLASMA("", 32, "") &&
              PLASMA("[Translations]\nLANGUAGEX=de\nLANGUAGE de\n", 32, ""),
          "no language where the key is missing, empty or names none");
    CHECK(PLASMA("[Translations]\nLANGUAGE=de_DE:fr_FR:it\n", 9, "de-DE,it"),
          "tags that do not fit left out whole");
    CHECK(PLASMA("[Translations]\nLANGUAGE=de\0\n", 32, "!"), "a NUL refused");
    char out[64];
    (void)unsetenv("LC_ALL");
    (void)unsetenv("LANGUAGE");
    (void)setenv("LC_MESSAGES", "fr_FR.UTF-8", 1);
    (void)setenv("LANG", "de_DE.UTF-8", 1);
    size_t length = mwinLinuxLocales(out, sizeof(out));
    CHECK(length == 5 && memcmp(out, "fr-FR", 5) == 0, "LC_MESSAGES before LANG");
    (void)setenv("LC_ALL", "it_IT.UTF-8", 1);
    length = mwinLinuxLocales(out, sizeof(out));
    CHECK(length == 5 && memcmp(out, "it-IT", 5) == 0, "LC_ALL before both");
    (void)unsetenv("LC_ALL");
    (void)setenv("LC_MESSAGES", "", 1);
    length = mwinLinuxLocales(out, sizeof(out));
    CHECK(length == 5 && memcmp(out, "de-DE", 5) == 0, "an empty LC_MESSAGES unheeded");
    return s_failures == 0 ? 0 : 1;
}
