// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland cursors from a cursor theme, against the test compositor of
// wayland_server.h without the cursor shape protocol, and a theme of
// the test's own that XCURSOR_PATH, XCURSOR_THEME and XCURSOR_SIZE
// name: the default shape at the size asked, of the two the theme has,
// with its hotspot; a shape the theme has only under its older X11
// name. Skipped (exit status 77) without XDG_RUNTIME_DIR or xkb data.

#include "test_harness.h"
#include "wayland_server.h"

#include "maul-window/event.h"
#include "maul-window/input.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 5000000000ull
// An Xcursor file's magic, and its image chunks' type.
#define XCURSOR_MAGIC 0x72756358u
#define XCURSOR_IMAGE 0xFFFD0002u

// Each image's first pixel, which tells them apart.
#define DEFAULT_16 0xFF102030u
#define DEFAULT_24 0xFF405060u
#define XTERM_16   0xFF708090u

typedef enum Phase
{
    phaseCreate,
    phaseDefault,
    phaseLegacy,
    phaseDone,
} Phase;

typedef struct Program
{
    Server* server;
    Phase phase;
    uint64_t startNs;
    mwinWindowId window;
    bool created;
    bool timedOut;
} Program;

static char s_directory[] = "/tmp/mwin-theme-XXXXXX";

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static bool Word(FILE* file, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                              (uint8_t)(value >> 24)};
    return fwrite(bytes, 1, 4, file) == 4;
}

// Writes an Xcursor file of square images, one per size, each filled
// with its pixel, its hotspot at a sixth and a fifth of its side.
static bool WriteCursor(const char* name, const uint32_t* sizes, const uint32_t* pixels,
                        uint32_t count)
{
    char path[64];
    (void)snprintf(path, sizeof(path), "%s/test/cursors/%s", s_directory, name);
    FILE* file = fopen(path, "wb");
    if (file == nullptr)
    {
        return false;
    }
    bool written =
        Word(file, XCURSOR_MAGIC) && Word(file, 16) && Word(file, 0x10000) && Word(file, count);
    uint32_t position = 16 + 12 * count;
    for (uint32_t i = 0; i < count && written; i++)
    {
        written = Word(file, XCURSOR_IMAGE) && Word(file, sizes[i]) && Word(file, position);
        position += 36 + 4 * sizes[i] * sizes[i];
    }
    for (uint32_t i = 0; i < count && written; i++)
    {
        written = Word(file, 36) && Word(file, XCURSOR_IMAGE) && Word(file, sizes[i]) &&
                  Word(file, 1) && Word(file, sizes[i]) && Word(file, sizes[i]) &&
                  Word(file, sizes[i] / 6) && Word(file, sizes[i] / 5) && Word(file, 0);
        for (uint32_t at = 0; at < sizes[i] * sizes[i] && written; at++)
        {
            written = Word(file, pixels[i]);
        }
    }
    return fclose(file) == 0 && written;
}

static bool WriteTheme(void)
{
    char path[64];
    (void)snprintf(path, sizeof(path), "%s/test", s_directory);
    bool made = mkdir(path, 0700) == 0;
    (void)snprintf(path, sizeof(path), "%s/test/cursors", s_directory);
    made = made && mkdir(path, 0700) == 0;
    static const uint32_t sizes[2] = {16, 24};
    static const uint32_t defaults[2] = {DEFAULT_16, DEFAULT_24};
    static const uint32_t xterm[1] = {XTERM_16};
    return made && WriteCursor("default", sizes, defaults, 2) &&
           WriteCursor("xterm", sizes, xterm, 1);
}

static void RemoveTheme(void)
{
    char path[64];
    (void)snprintf(path, sizeof(path), "%s/test/cursors/default", s_directory);
    (void)unlink(path);
    (void)snprintf(path, sizeof(path), "%s/test/cursors/xterm", s_directory);
    (void)unlink(path);
    (void)snprintf(path, sizeof(path), "%s/test/cursors", s_directory);
    (void)rmdir(path);
    (void)snprintf(path, sizeof(path), "%s/test", s_directory);
    (void)rmdir(path);
    (void)rmdir(s_directory);
}

static bool Ready(const Program* program)
{
    Cursor cursor = ServerCursor(program->server);
    switch (program->phase)
    {
    case phaseCreate:
        return program->created;
    case phaseDefault:
        return cursor.images >= 1;
    case phaseLegacy:
        return cursor.images >= 2;
    default:
        return false;
    }
}

static void Advance(Program* program, mwinContext* context)
{
    Cursor cursor = ServerCursor(program->server);
    switch (program->phase)
    {
    case phaseCreate:
        ServerPointerEnter(program->server, 10.0, 20.0);
        break;
    case phaseDefault:
        CHECK(cursor.surfaceShown && cursor.width == 16 && cursor.height == 16 &&
                  cursor.pixel == DEFAULT_16 && cursor.hotspotX == 2 && cursor.hotspotY == 3,
              "the default shape at the size asked, with its hotspot");
        CHECK(mwinRequestCursorShape(context, program->window, mwin_shapeText, nullptr) ==
                  mwin_success,
              "the text shape");
        break;
    default:
        CHECK(cursor.surfaceShown && cursor.width == 16 && cursor.pixel == XTERM_16,
              "the text shape under its older name");
        break;
    }
    program->phase += 1;
    program->startNs = NowNs();
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    program->startNs = NowNs();
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->created = program->created || event.type == mwin_eventWindowCreated;
    }
    if (Ready(program))
    {
        Advance(program, context);
    }
    else if (NowNs() - program->startNs > DEADLINE_NS)
    {
        (void)printf("timed out in phase %d\n", (int)program->phase);
        program->timedOut = true;
        return mwin_frameStop;
    }
    else
    {
        struct timespec pause = {0, 500000};
        (void)nanosleep(&pause, nullptr);
    }
    return program->phase == phaseDone ? mwin_frameStop : mwin_frameContinue;
}

int main(void)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    static Server server;
    if (runtime == nullptr || runtime[0] == '\0' || mkdtemp(s_directory) == nullptr)
    {
        return 77;
    }
    if (!WriteTheme() || !ServerStart(&server, "us", ""))
    {
        RemoveTheme();
        return 77;
    }
    ServerRemoveShapes(&server);
    (void)setenv("XCURSOR_PATH", s_directory, 1);
    (void)setenv("XCURSOR_THEME", "test", 1);
    (void)setenv("XCURSOR_SIZE", "16", 1);
    static Program program;
    program = (Program){.server = &server};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success, "the program runs on the test compositor");
    CHECK(!program.timedOut, "every phase completes in time");
    CHECK(program.phase == phaseDone, "every phase ran");
    ServerStop(&server);
    RemoveTheme();
    return s_failures == 0 ? 0 : 1;
}
