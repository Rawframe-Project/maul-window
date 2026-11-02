// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text from other programs, made well-formed, and text a program hands
// over, checked.

#ifndef MAUL_WINDOW_SRC_UTF8_H
#define MAUL_WINDOW_SRC_UTF8_H

#include "maul-window/base.h"

#include <stddef.h>
#include <stdint.h>

// The length of text with each maximal ill-formed subpart replaced with
// U+FFFD, written to out unless it is NULL. It is never shorter.
size_t mwinRepairUtf8(const char* bytes, size_t length, char* out);

// Writes the UTF-8 of UTF-16 units to out, the whole characters that fit
// in capacity, each unpaired surrogate as U+FFFD, and returns its length.
// It reads at most capacity + 2 units, however many there are.
size_t mwinCutUtf16(const uint16_t* units, size_t count, char* out, size_t capacity);

// Checks text a program hands over: mwin_errorInvalid for NULL with a
// length; over, as the bound is a named one (mwin_errorLimit) or one of
// mwinLimits (mwin_errorCapacity), for more bytes than limit; then
// mwin_errorInvalid for bytes that are not UTF-8, or that hold a NUL
// unless nul allows one; else mwin_success. The length is checked before
// the bytes, so text past its bound is never read.
mwinResult mwinCheckText(const char* text, size_t length, size_t limit, mwinResult over, bool nul);

#endif // MAUL_WINDOW_SRC_UTF8_H
