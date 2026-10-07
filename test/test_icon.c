// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Window icons on the test backend: images refused at the call (too
// many, empty, too large, without pixels, rows longer than their
// stride); two images copied without their rows' padding; none for the
// platform's own; an icon's images given back with its window or at the
// end (the sanitizer builds would report a leak). And the image each
// platform takes for a size: the smallest at least as large, else the
// largest.

#include "icon.h"
#include "test_program.h"

#include "maul-window/test.h"

#include <stddef.h>
#include <string.h>

// 16 by 16 with rows padded by 8 bytes, and 32 by 32 packed.
static uint8_t s_small[16 * (16 * 4 + 8)];
static uint8_t s_large[32 * 32 * 4];

static uint64_t Hash(uint64_t hash, const uint8_t* bytes, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        hash = (hash ^ bytes[i]) * 0x100000001B3u;
    }
    return hash;
}

// The checksum of an image as the test backend makes it: its size, then
// its rows without their padding.
static uint64_t HashImage(uint64_t hash, const mwinIconImage* image)
{
    uint8_t size[8];
    for (int b = 0; b < 4; b++)
    {
        size[b] = (uint8_t)(image->width >> (8 * b));
        size[4 + b] = (uint8_t)(image->height >> (8 * b));
    }
    hash = Hash(hash, size, sizeof(size));
    for (uint32_t y = 0; y < image->height; y++)
    {
        hash = Hash(hash, image->pixels + y * image->stride, (size_t)image->width * 4);
    }
    return hash;
}

static const mwinIconImage s_images[2] = {
    {16, 16, 16 * 4 + 8, s_small},
    {32, 32, 32 * 4, s_large},
};

static bool Refused(mwinContext* context, mwinWindowId window, mwinIconImage image)
{
    return mwinRequestIcon(context, window, &image, 1, nullptr) == mwin_errorInvalid;
}

static void CheckRefusals(mwinContext* context, mwinWindowId window)
{
    mwinIconImage five[5] = {s_images[1], s_images[1], s_images[1], s_images[1], s_images[1]};
    CHECK(mwinRequestIcon(context, window, five, 5, nullptr) == mwin_errorInvalid &&
              mwinRequestIcon(context, window, nullptr, 1, nullptr) == mwin_errorInvalid &&
              mwinRequestIcon(nullptr, window, s_images, 1, nullptr) == mwin_errorInvalid &&
              Refused(context, window, (mwinIconImage){0, 16, 64, s_large}) &&
              Refused(context, window, (mwinIconImage){16, 0, 64, s_large}) &&
              Refused(context, window, (mwinIconImage){257, 1, 257 * 4, s_large}) &&
              Refused(context, window, (mwinIconImage){16, 16, 16 * 4, nullptr}) &&
              Refused(context, window, (mwinIconImage){16, 16, 16 * 4 - 1, s_large}),
          "images refused at the call");
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

static bool Holds(mwinContext* context, uint32_t expectedCount, uint64_t expectedChecksum)
{
    uint32_t count = 0;
    uint64_t checksum = 0;
    return mwinTestGetIcon(context, &count, &checksum) == mwin_success && count == expectedCount &&
           checksum == expectedChecksum;
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    mwinWindowId window = program->windows[0];
    uint64_t both = HashImage(HashImage(0xCBF29CE484222325u, &s_images[0]), &s_images[1]);
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        break;
    case 1:
        CheckRefusals(context, window);
        CHECK(mwinRequestIcon(context, window, s_images, 2, &program->requests[1]) == mwin_success,
              "two images");
        break;
    case 2:
        CHECK(Outcome(program, 1) == mwin_outcomeDone && Holds(context, 2, both),
              "two images copied without their rows' padding");
        CHECK(mwinRequestIcon(context, window, nullptr, 0, &program->requests[2]) == mwin_success,
              "none");
        break;
    case 3:
        CHECK(Outcome(program, 2) == mwin_outcomeDone && Holds(context, 0, 0xCBF29CE484222325u),
              "none for the platform's own");
        // Held: one icon is cancelled with its window, one never answered.
        program->windows[1] = Create(context, nullptr);
        CHECK(mwinTestHold(context, true) == mwin_success &&
                  mwinRequestIcon(context, window, s_images, 2, &program->requests[3]) ==
                      mwin_success &&
                  mwinRequestIcon(context, program->windows[1], s_images, 1, nullptr) ==
                      mwin_success &&
                  mwinDestroyWindow(context, window) == mwin_success,
              "icons held, one window destroyed");
        break;
    default:
        CHECK(Outcome(program, 3) == mwin_outcomeCancelled, "cancelled with its window");
        program->done = true;
        break;
    }
}

// The image each platform takes for a size.
static void CheckPick(void)
{
    mwinIconCopyImage images[3] = {{48, 48, nullptr}, {16, 16, nullptr}, {32, 24, nullptr}};
    static max_align_t storage[(sizeof(mwinIconCopy) + sizeof(images)) / sizeof(max_align_t) + 1];
    mwinIconCopy* icon = (mwinIconCopy*)storage;
    icon->count = 3;
    memcpy(icon->images, images, sizeof(images));
    CHECK(mwinIconFor(icon, 16) == &icon->images[1] && mwinIconFor(icon, 20) == &icon->images[2] &&
              mwinIconFor(icon, 33) == &icon->images[0] &&
              mwinIconFor(icon, 64) == &icon->images[0],
          "the smallest image at least as large, else the largest");
    // Two of one side, too small either way: the first stays.
    icon->images[0] = (mwinIconCopyImage){16, 8, nullptr};
    icon->images[1] = (mwinIconCopyImage){8, 16, nullptr};
    icon->count = 2;
    CHECK(mwinIconFor(icon, 64) == &icon->images[0], "of two the same, the first");
    icon->count = 0;
    CHECK(mwinIconFor(icon, 16) == nullptr, "none without images");
}

int main(void)
{
    for (size_t i = 0; i < sizeof(s_small); i++)
    {
        s_small[i] = (uint8_t)(i * 7);
    }
    for (size_t i = 0; i < sizeof(s_large); i++)
    {
        s_large[i] = (uint8_t)(i * 13 + 5);
    }
    CheckPick();
    Program program = {.step = Step};
    CHECK(Run(&program) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
