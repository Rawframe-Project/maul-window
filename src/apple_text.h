// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text helpers the macOS and iOS backends share: where a UTF-16 index
// of an NSString falls in its UTF-8 bytes, as compositions report
// their carets and clauses; and another program's text taken by its
// UTF-16 units. Included by Objective-C files only.

#ifndef MAUL_WINDOW_SRC_APPLE_TEXT_H
#define MAUL_WINDOW_SRC_APPLE_TEXT_H

#include "core.h"

#import <Foundation/Foundation.h>
#include <stdint.h>

// The UTF-8 bytes of the first units of a UTF-16 string, a lone
// surrogate as the three of U+FFFD.
uint32_t mwinAppleBytesBefore(NSString* string, NSUInteger index);

// Another program's text as the clipboard's (mwinTakeClipboardUtf16) or
// the drop's (mwinSetDroppedTextUtf16), every unit of it: U+0000 kept,
// a lone surrogate as U+FFFD. UTF8String would end the text at U+0000,
// and gives none for a lone surrogate.
mwinOutcome mwinAppleTakeClipboardText(mwinContext* context, NSString* text);
void mwinAppleSetDroppedText(mwinContext* context, NSString* text);

// A string as UTF-8, every unit of it, a lone surrogate as U+FFFD, as
// mwinAppleBytesBefore counts it: *lengthOut bytes in a block of the
// context's of *sizeOut bytes, which the caller lets go. NULL for an
// empty string, or with no memory.
char* mwinAppleUtf8Of(mwinContext* context, NSString* string, size_t* lengthOut, size_t* sizeOut);

#endif // MAUL_WINDOW_SRC_APPLE_TEXT_H
