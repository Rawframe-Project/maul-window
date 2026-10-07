// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scale of Xft.dpi in the root window's resources:
// - read at the start of the text or of a line, past spaces and tabs,
//   and nowhere else in a line;
// - a fraction after one decimal point, a second point ending the
//   number;
// - the number ending where the text's length does;
// - a quarter and 8 the scales allowed, 1 past them or without Xft.dpi.

#include "test_harness.h"
#include "x11_resources.h"

#include <string.h>

static float Scale(const char* text)
{
    return mwinX11ScaleOfResources(text, strlen(text));
}

static bool Near(float value, float expected)
{
    float difference = value - expected;
    return difference < 1e-5f && difference > -1e-5f;
}

int main(void)
{
    CHECK(Scale("Xft.dpi: 192") == 2.0f && Scale("Xft.dpi:192") == 2.0f &&
              Scale("Xcursor.size: 24\nXft.dpi:\t 144\nXft.hinting: 1") == 1.5f,
          "the start of the text or of a line, past spaces and tabs");
    CHECK(Scale("Foo.Xft.dpi: 192") == 1.0f && Scale("a Xft.dpi: 192") == 1.0f &&
              Scale("Xcursor.size: 24") == 1.0f && Scale("") == 1.0f,
          "nowhere else in a line, and nothing without it");
    CHECK(Near(Scale("Xft.dpi: 120.5"), 120.5f / 96.0f) &&
              Near(Scale("Xft.dpi: 96.5.5"), 96.5f / 96.0f) && Near(Scale("Xft.dpi: .25e3"), 1.0f),
          "a fraction, and a second point ending the number");
    CHECK(mwinX11ScaleOfResources("Xft.dpi: 1920", 12) == 2.0f,
          "the number ends with the text's length");
    CHECK(Scale("Xft.dpi: 24") == 0.25f && Scale("Xft.dpi: 768") == 8.0f &&
              Scale("Xft.dpi: 23.9") == 1.0f && Scale("Xft.dpi: 768.1") == 1.0f,
          "a quarter to 8, 1 past them");
    return s_failures == 0 ? 0 : 1;
}
