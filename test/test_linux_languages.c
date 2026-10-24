// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The desktop's languages followed while the program runs, on X11,
// against AccountsService on a system bus of the test's own
// (linux_bus_fake.h) and plasma-localerc in a configuration folder of
// its own. The preferred locales start from the environment, en-US:
// - Each source's first word only its baseline: nothing told.
// - The baseline's repeat, a Languages of the wrong type, a change of
//   another interface and one keyed by numbers (a{tv}): nothing told.
// - A change of the user's Languages told; its repeat tells nothing.
// - Languages that name no language: back to the starting list.
// - The older Changed signal: the properties read again.
// - Language alone, where Languages is not in the change.
// - plasma-localerc renamed into place with its baseline's LANGUAGE,
//   then written in place with another: told once.
// - plasma-localerc with a NUL, too long, or written again with the
//   same LANGUAGE: nothing told.
// - The user's repeat after plasma-localerc's change: nothing told.
// - Another file of the folder written: plasma-localerc not read.
// - The user's change from another program on the bus, to every listener
//   or to the program alone: nothing told.
// - AccountsService restarting under a new unique name: a change from the
//   old connection while no one holds the name tells nothing; the user
//   found and read again, nothing told where the list is the same; a
//   change from the old connection or another program then tells nothing,
//   and one from the new connection is followed.
// - A restart right behind the new owner's answer to FindUserById: the
//   user found once more from the owner after it, nothing told for the
//   same list, and that owner's change followed.
// - With no system bus and no configuration folder, or AccountsService
//   refusing the user and plasma-localerc no file: the starting list,
//   and nothing told.
// A step that tells nothing on the bus ends with Changed, and is judged
// when the GetAll that follows reaches the stand-in: the library read
// what came before it by then. One on the file is judged a few frames
// on: the watch's events are there at the next pump.
// Not exercised: the watch's queue overflowing, which takes thousands
// of events the folder never sees here.
// Skipped (exit status 77) without an X server, dbus-daemon or
// libdbus-1; a bus that cannot start fails it.

#include "linux_bus_fake.h"
#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS  10000000000ull
#define QUIET_FRAMES 3

static char s_directory[] = "/tmp/mwin-languages-XXXXXX";
static char s_system[64];
static char s_config[64];
static char s_file[96];

typedef enum Kind
{
    // The locales change to the step's.
    kindChange,
    // Nothing told on the bus, judged at the next GetAll.
    kindQuietBus,
    // Nothing told by the file, judged QUIET_FRAMES frames on.
    kindQuietFile,
    // Nothing told on the bus, judged at the first GetAll after the user
    // was found twice.
    kindFoundTwice,
} Kind;

typedef struct Expected
{
    Kind kind;
    const char* locales;
} Expected;

static const Expected s_steps[] = {
    {kindQuietBus, "en-US"},     {kindChange, "de-DE,en-US"}, {kindQuietBus, "de-DE,en-US"},
    {kindChange, "en-US"},       {kindChange, "pt-BR"},       {kindChange, "sv-SE"},
    {kindChange, "es,ca-ES"},    {kindQuietFile, "es,ca-ES"}, {kindQuietFile, "es,ca-ES"},
    {kindQuietFile, "es,ca-ES"}, {kindQuietBus, "es,ca-ES"},  {kindQuietFile, "es,ca-ES"},
    {kindQuietBus, "es,ca-ES"},  {kindQuietBus, "es,ca-ES"},  {kindQuietBus, "es,ca-ES"},
    {kindChange, "fi-FI"},       {kindFoundTwice, "fi-FI"},   {kindChange, "ko-KR"},
    {kindChange, "nl"},
};

typedef struct Program
{
    FakeBus* system;
    int step;
    int steps;
    int changes;
    int frames;
    int quiet;
    int getAlls;
    int finds;
    uint64_t startNs;
    bool done;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Write(const char* path, const char* text, size_t length)
{
    FILE* file = fopen(path, "wb");
    CHECK(file != nullptr && fwrite(text, 1, length, file) == length && fclose(file) == 0,
          "plasma-localerc written");
}

#define WRITE(path, text) Write(path, text, sizeof(text) - 1)

// The user's next GetAll answers with these.
static void User(FakeBus* system, const char* languages, const char* language)
{
    (void)snprintf(system->languages, sizeof(system->languages), "%s", languages);
    (void)snprintf(system->language, sizeof(system->language), "%s", language);
}

// What the desktop does at the start of a step.
static void Act(Program* program)
{
    FakeBus* system = program->system;
    const char* text = "es";
    program->getAlls = system->getAlls;
    program->finds = system->finds;
    switch (program->step)
    {
    case 0:
        FakeLanguagesChanged(system, "fr_FR");
        FakePropertyChanged(system, FAKE_USER, FAKE_ACCOUNTS_USER, "Languages", 's', &text);
        FakePropertyChanged(system, FAKE_USER, "org.freedesktop.Accounts.Other", "Language", 's',
                            &text);
        FakeOddChanged(system, FAKE_USER, FAKE_ACCOUNTS_USER);
        FakeUserChanged(system);
        break;
    case 1:
        FakeLanguagesChanged(system, "de_DE:en_US");
        break;
    case 2:
        // Changed twice: the second waits while the first is handled.
        User(system, "de_DE:en_US", "");
        FakeLanguagesChanged(system, "de_DE:en_US");
        FakeUserChanged(system);
        FakeUserChanged(system);
        break;
    case 3:
        FakeLanguagesChanged(system, "C:x1:english");
        break;
    case 4:
        User(system, "pt_BR", "");
        FakeUserChanged(system);
        break;
    case 5:
        text = "sv_SE.UTF-8";
        FakePropertyChanged(system, FAKE_USER, FAKE_ACCOUNTS_USER, "Language", 's', &text);
        break;
    case 6:
    {
        char temporary[112];
        (void)snprintf(temporary, sizeof(temporary), "%s.new", s_file);
        WRITE(temporary, "[Translations]\nLANGUAGE=de\n");
        CHECK(rename(temporary, s_file) == 0, "plasma-localerc renamed");
        WRITE(s_file, "[Translations]\nLANGUAGE=es:ca_ES\n");
        break;
    }
    case 7:
        WRITE(s_file, "[Translations]\nLANGUAGE=ru\n\0\n");
        break;
    case 8:
    {
        // Past the 4096 bytes the library reads.
        static char longer[4160];
        memset(longer, '#', sizeof(longer));
        memcpy(longer, "[Translations]\nLANGUAGE=ru\n", 27);
        Write(s_file, longer, sizeof(longer));
        break;
    }
    case 9:
        WRITE(s_file, "[Translations]\nLANGUAGE=es:ca_ES\n");
        break;
    case 10:
        // An empty Languages leaves Language to say it.
        User(system, "", "sv_SE.UTF-8");
        text = "sv_SE.UTF-8";
        FakePropertyChanged(system, FAKE_USER, FAKE_ACCOUNTS_USER, "Language", 's', &text);
        FakeUserChanged(system);
        break;
    case 11:
    {
        // Changed through a link outside the folder, which the watch does
        // not see, then another file of the folder written: not read.
        char path[112];
        (void)snprintf(path, sizeof(path), "%s/link", s_directory);
        CHECK(link(s_file, path) == 0, "plasma-localerc linked");
        WRITE(path, "[Translations]\nLANGUAGE=fi\n");
        (void)remove(path);
        (void)snprintf(path, sizeof(path), "%s/kdeglobals", s_config);
        WRITE(path, "[General]\n");
        (void)remove(path);
        break;
    }
    case 12:
        CHECK(FakeSpoof(system, "ja_JP"), "another program sent the user's change");
        FakeUserChanged(system);
        break;
    case 13:
        // The same user, under the same list; the old connection's change
        // between the two steps.
        CHECK(FakeRelease(system) &&
                  FakeSpoofFrom(system, system->retired, "ja_JP", "org.example.Gap") &&
                  FakeTakeOver(system),
              "AccountsService restarted");
        break;
    case 14:
        CHECK(FakeSpoofFrom(system, system->retired, "ja_JP", "org.example.Old") &&
                  FakeSpoof(system, "ja_JP"),
              "the old connection and another program sent the user's change");
        FakeUserChanged(system);
        break;
    case 15:
        FakeLanguagesChanged(system, "fi_FI");
        break;
    case 16:
        // A restart whose FindUserById answer the owner change follows at
        // once: the program has the answer and the change in one read.
        User(system, "fi_FI", "");
        system->restartAfterFind = true;
        CHECK(FakeRelease(system) && FakeTakeOver(system), "AccountsService restarted");
        break;
    case 17:
        FakeLanguagesChanged(system, "ko_KR");
        break;
    default:
        WRITE(s_file, "[Translations]\nLANGUAGE=nl\n");
        break;
    }
}

static bool LocalesAre(mwinContext* context, const char* expected)
{
    char locales[64];
    size_t length = 0;
    return mwinGetPreferredLocales(context, locales, sizeof(locales), &length) == mwin_success &&
           length == strlen(expected) && memcmp(locales, expected, length) == 0;
}

static mwinResult Init(mwinContext* context, void* user)
{
    (void)context;
    Program* program = user;
    program->startNs = NowNs();
    return mwin_success;
}

// The steps: once the user's properties were read, each acts and waits
// for its locales, or for nothing told.
static void Step(mwinContext* context, Program* program)
{
    if (program->step < 0)
    {
        if (program->system->getAlls == 0)
        {
            return;
        }
        CHECK(LocalesAre(context, "en-US") && program->changes == 0,
              "the sources' first words only their baselines");
        program->step = 0;
        Act(program);
        return;
    }
    const Expected* expected = &s_steps[program->step];
    const FakeBus* system = program->system;
    bool reached = expected->kind == kindChange     ? LocalesAre(context, expected->locales)
                   : expected->kind == kindQuietBus ? system->getAlls > program->getAlls
                   : expected->kind == kindFoundTwice
                       ? system->finds >= program->finds + 2 && system->getAllsSinceFind > 0
                       : ++program->quiet == QUIET_FRAMES;
    if (!reached)
    {
        return;
    }
    if (expected->kind == kindChange)
    {
        CHECK(program->changes == 1, "a change told once");
    }
    else
    {
        CHECK(program->changes == 0 && LocalesAre(context, expected->locales),
              program->step == 10   ? "the user's repeat after the file's change tells nothing"
              : program->step == 11 ? "another file's write reads nothing"
              : program->step == 12 ? "another program's change of the user tells nothing"
              : program->step == 13 ? "a restart with the same list tells nothing"
              : program->step == 14 ? "the old connection's change tells nothing"
              : program->step == 16 ? "an owner change right behind the user's answer tells "
                                      "nothing"
              : expected->kind == kindQuietBus ? "repeats and odd changes on the bus tell nothing"
                                               : "a file with a NUL, too long, or with the same "
                                                 "LANGUAGE tells nothing");
    }
    CHECK(program->step != 4 || program->system->getAlls == program->getAlls + 1,
          "Changed reads the user again");
    CHECK(program->step != 13 || program->system->finds == 2,
          "a restart finds the user again, once");
    CHECK(program->step != 16 ||
              (program->system->restartedAfterFind && program->system->finds == program->finds + 2),
          "a restart behind the answer finds the user once more");
    program->changes = 0;
    program->quiet = 0;
    program->step += 1;
    program->done = program->step == program->steps;
    if (!program->done)
    {
        Act(program);
    }
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    // Before the events: a GetAll it reads now was sent after all that
    // came before it was taken, and its answer is not taken yet.
    FakePump(program->system);
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        // The starting list's own change comes before the first frame.
        program->changes += event.type == mwin_eventLocaleChanged && program->frames > 0;
    }
    program->frames += 1;
    if (program->steps == 0)
    {
        CHECK(LocalesAre(context, "en-US") && program->changes == 0,
              "nothing to follow leaves the starting list");
        program->done = program->frames > 50;
    }
    else
    {
        Step(context, program);
    }
    struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, nullptr);
    bool late = NowNs() - program->startNs > DEADLINE_NS;
    return program->done || late ? mwin_frameStop : mwin_frameContinue;
}

static void Run(FakeBus* system, int steps, const char* what)
{
    Program program = {.system = system, .step = -1, .steps = steps};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, what);
}

int main(void)
{
    static FakeBus system;
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        return 77;
    }
    if (mkdtemp(s_directory) == nullptr)
    {
        perror("FAIL: mkdtemp");
        return 1;
    }
    (void)unsetenv("WAYLAND_DISPLAY");
    (void)unsetenv("LANGUAGE");
    (void)unsetenv("LC_ALL");
    (void)unsetenv("LC_MESSAGES");
    (void)setenv("LANG", "en_US.UTF-8", 1);
    (void)snprintf(s_system, sizeof(s_system), "%s/system", s_directory);
    (void)snprintf(s_config, sizeof(s_config), "%s/config", s_directory);
    (void)snprintf(s_file, sizeof(s_file), "%s/plasma-localerc", s_config);
    (void)mkdir(s_system, 0700);
    (void)mkdir(s_config, 0700);
    system.system = true;
    int status = 0;
    FakeStarted started = FakeStart(&system, s_system);
    if (started != fakeRunning)
    {
        status = FakeNotRunning(&system, started);
    }
    else
    {
        User(&system, "fr_FR", "");
        (void)setenv("XDG_CONFIG_HOME", s_config, 1);
        WRITE(s_file, "[Translations]\nLANGUAGE=de\n");
        Run(&system, (int)(sizeof(s_steps) / sizeof(s_steps[0])),
            "the desktop's languages followed");
        // plasma-localerc a folder, and AccountsService refusing the user.
        (void)remove(s_file);
        (void)mkdir(s_file, 0700);
        system.noUser = true;
        system.finds = 0;
        Run(&system, 0, "a refused user and no plasma-localerc file");
        CHECK(system.finds == 1, "the user asked for once");
        (void)rmdir(s_file);
        // Neither the bus nor the folder.
        (void)setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/mwin", 1);
        (void)setenv("XDG_CONFIG_HOME", "/nonexistent/mwin", 1);
        Run(nullptr, 0, "no system bus and no configuration folder");
        status = s_failures == 0 ? 0 : 1;
    }
    FakeStop(&system);
    (void)remove(s_file);
    (void)rmdir(s_config);
    FakeClean(s_system);
    (void)rmdir(s_system);
    (void)rmdir(s_directory);
    return status;
}
