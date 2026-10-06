// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The cursor the backends' input tests make from images: red at 16
// pixels for scale 1, green at 32 for scale 2, its hotspot at (3, 5).

#ifndef MAUL_WINDOW_TEST_CURSOR_IMAGES_H
#define MAUL_WINDOW_TEST_CURSOR_IMAGES_H

#include "maul-window/input.h"

#include <string.h>

static uint8_t s_cursorRed[16 * 16 * 4];
static uint8_t s_cursorGreen[32 * 32 * 4];

// The def, its images in images.
static inline mwinCursorDef CursorImagesDef(mwinIconImage images[2])
{
    for (size_t i = 0; i < sizeof(s_cursorGreen); i += 4)
    {
        memcpy(&s_cursorGreen[i], (const uint8_t[]){0, 255, 0, 255}, 4);
        if (i < sizeof(s_cursorRed))
        {
            memcpy(&s_cursorRed[i], (const uint8_t[]){255, 0, 0, 255}, 4);
        }
    }
    images[0] = (mwinIconImage){16, 16, 16 * 4, s_cursorRed};
    images[1] = (mwinIconImage){32, 32, 32 * 4, s_cursorGreen};
    mwinCursorDef def = mwinDefaultCursorDef();
    def.images = images;
    def.imageCount = 2;
    def.hotspotX = 3;
    def.hotspotY = 5;
    return def;
}

#endif // MAUL_WINDOW_TEST_CURSOR_IMAGES_H
