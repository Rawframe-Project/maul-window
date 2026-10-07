// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors made from images on the test backend (mwin-0027): defs refused
// at the call; a cursor shown over a window, taking its image for the
// scale; a shape in its place; a destroyed cursor leaving its window
// the default shape and its id stale; the limit; cursors still live at
// the end given back (the sanitizer builds would report a leak). And
// the image each scale takes, with its hotspot scaled.

#include "cursor.h"
#include "test_program.h"

#include "maul-window/test.h"

#include <stdlib.h>
#include <string.h>

static uint8_t s_small[16 * 16 * 4];
static uint8_t s_large[32 * (32 * 4 + 4)];

static const mwinIconImage s_images[2] = {
    {16, 16, 16 * 4, s_small},
    {32, 32, 32 * 4 + 4, s_large},
};

static mwinCursorId s_cursor;

static mwinCursorDef Def(const mwinIconImage* images, uint32_t count)
{
    mwinCursorDef def = mwinDefaultCursorDef();
    def.images = images;
    def.imageCount = count;
    def.hotspotX = 3;
    def.hotspotY = 5;
    return def;
}

static bool Refused(mwinContext* context, mwinCursorDef def)
{
    mwinCursorId cursor;
    return mwinCreateCursor(context, &def, &cursor) == mwin_errorInvalid;
}

static void CheckRefusals(mwinContext* context)
{
    uint64_t misuse = mwinGetContextMisuse(context);
    mwinCursorDef def = Def(s_images, 2);
    mwinCursorId cursor;
    mwinIconImage five[5] = {s_images[0], s_images[0], s_images[0], s_images[0], s_images[0]};
    mwinIconImage big = {129, 16, 129 * 4, s_large};
    mwinIconImage narrower[2] = {s_images[1], s_images[0]};
    mwinCursorDef cookie = def;
    cookie.cookie = 0;
    mwinCursorDef outside = def;
    outside.hotspotX = 16;
    CHECK(mwinCreateCursor(context, nullptr, &cursor) == mwin_errorInvalid &&
              mwinCreateCursor(context, &def, nullptr) == mwin_errorInvalid &&
              mwinCreateCursor(nullptr, &def, &cursor) == mwin_errorInvalid &&
              Refused(context, cookie) && Refused(context, Def(s_images, 0)) &&
              Refused(context, Def(five, 5)) && Refused(context, Def(&big, 1)) &&
              Refused(context, Def(narrower, 2)) && Refused(context, outside),
          "defs refused at the call");
    CHECK(mwinGetContextMisuse(context) == misuse + 8, "each counted");
    // The most images, each wider than the one before.
    static uint8_t pixels[48 * 48 * 4];
    const mwinIconImage four[MWIN_CURSOR_IMAGES] = {
        {16, 16, 16 * 4, pixels},
        {24, 24, 24 * 4, pixels},
        {32, 32, 32 * 4, pixels},
        {48, 48, 48 * 4, pixels},
    };
    mwinCursorDef most = Def(four, MWIN_CURSOR_IMAGES);
    CHECK(mwinCreateCursor(context, &most, &cursor) == mwin_success &&
              mwinDestroyCursor(context, cursor) == mwin_success,
          "a cursor of the most images");
}

static int Outcome(const Program* program, int request)
{
    for (int i = 0; i < program->eventCount; i++)
    {
        const mwinEvent* event = &program->events[i];
        if (event->type == mwin_eventRequestCompleted &&
            SameId(event->data.completion.request, program->requests[request]))
        {
            return event->data.completion.outcome;
        }
    }
    return -1;
}

// Whether the window shows the cursor with that image, or a shape for a
// zero cursor.
static bool Shows(mwinContext* context, mwinWindowId window, mwinCursorId cursor, uint32_t image)
{
    mwinCursorId shown = {1, 1};
    uint32_t taken = 99;
    return mwinTestGetCursor(context, window, &shown, &taken) == mwin_success &&
           shown.index1 == cursor.index1 && shown.generation == cursor.generation &&
           (cursor.index1 == 0 || taken == image);
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinWindowId window = program->windows[0];
    mwinCursorId none = {0};
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        break;
    case 1:
    {
        CheckRefusals(context);
        mwinCursorDef def = Def(s_images, 2);
        CHECK(mwinCreateCursor(context, &def, &s_cursor) == mwin_success && s_cursor.index1 != 0,
              "a cursor of two images");
        CHECK(mwinRequestCursorImage(context, window, s_cursor, &program->requests[1]) ==
                  mwin_success,
              "shown");
        break;
    }
    case 2:
        CHECK(Outcome(program, 1) == mwin_outcomeDone && Shows(context, window, s_cursor, 0),
              "the first image at scale 1");
        CHECK(mwinTestSetScale(context, 2.0f) == mwin_success &&
                  mwinRequestCursorImage(context, window, s_cursor, nullptr) == mwin_success,
              "shown at scale 2");
        break;
    case 3:
        CHECK(Shows(context, window, s_cursor, 1), "the second image at scale 2");
        CHECK(mwinRequestCursorShape(context, window, mwin_shapeText, nullptr) == mwin_success,
              "a shape");
        break;
    case 4:
        CHECK(Shows(context, window, none, 0), "the shape in its place");
        CHECK(mwinRequestCursorImage(context, window, s_cursor, nullptr) == mwin_success,
              "the cursor again");
        break;
    case 5:
    {
        CHECK(Shows(context, window, s_cursor, 1) &&
                  mwinDestroyCursor(context, s_cursor) == mwin_success &&
                  Shows(context, window, none, 0),
              "destroyed, its window shows the default shape");
        CHECK(mwinDestroyCursor(context, s_cursor) == mwin_errorStale &&
                  mwinRequestCursorImage(context, window, s_cursor, nullptr) == mwin_errorStale &&
                  mwinDestroyCursor(nullptr, s_cursor) == mwin_errorInvalid &&
                  mwinRequestCursorImage(nullptr, window, s_cursor, nullptr) == mwin_errorInvalid,
              "its id stale");
        // The default limit, 16; these stay live until the end.
        mwinCursorDef def = Def(s_images, 1);
        mwinCursorId cursor;
        mwinCursorId first = {0};
        bool made = true;
        for (int i = 0; i < 16; i++)
        {
            made = made && mwinCreateCursor(context, &def, &cursor) == mwin_success;
            first = i == 0 ? cursor : first;
        }
        CHECK(made && mwinCreateCursor(context, &def, &cursor) == mwin_errorCapacity,
              "16 cursors, then the limit");
        CHECK(first.index1 == s_cursor.index1 && first.generation == s_cursor.generation + 1,
              "a slot reused with the next generation");
        CHECK(mwinRequestCursorImage(context, window, first, nullptr) == mwin_success,
              "shown at the end");
        program->done = true;
        break;
    }
    default:
        break;
    }
}

// The image each scale takes, and its hotspot.
static void CheckPick(void)
{
    mwinIconCopyImage images[3] = {{16, 16, nullptr}, {24, 24, nullptr}, {32, 32, nullptr}};
    // Exactly the copy's size, so that AddressSanitizer sees a read past
    // its last image.
    mwinIconCopy* copy = malloc(sizeof(mwinIconCopy) + sizeof(images));
    if (copy == nullptr)
    {
        return;
    }
    copy->count = 3;
    memcpy(copy->images, images, sizeof(images));
    mwinCursor cursor = {.generation = 1, .images = copy, .hotspotX = 5, .hotspotY = 15};
    CHECK(mwinCursorImageFor(&cursor, 1.0f) == 0 && mwinCursorImageFor(&cursor, 0.5f) == 0 &&
              mwinCursorImageFor(&cursor, 1.25f) == 1 && mwinCursorImageFor(&cursor, 1.5f) == 1 &&
              mwinCursorImageFor(&cursor, 1.75f) == 2 && mwinCursorImageFor(&cursor, 3.0f) == 2,
          "the smallest at least the scaled size, else the largest");
    uint32_t x = 0;
    uint32_t y = 0;
    mwinCursorHotspotOf(&cursor, 2, &x, &y);
    CHECK(x == 10 && y == 30, "the hotspot scaled");
    mwinCursorHotspotOf(&cursor, 1, &x, &y);
    CHECK(x == 7 && y == 22, "rounded down");
    mwinCursorHotspotOf(&cursor, 0, &x, &y);
    CHECK(x == 5 && y == 15, "as given on the first");
    free(copy);
}

int main(void)
{
    for (size_t i = 0; i < sizeof(s_small); i++)
    {
        s_small[i] = (uint8_t)(i * 7);
    }
    CheckPick();
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
