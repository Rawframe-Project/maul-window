// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Linux services against stand-ins: xdg-open, a script on PATH
// that writes its argument down and exits as told, and a file manager
// of the test's own on a private session bus. Without a bus, on X11: an
// address opened by xdg-open, its statuses for no tool and a failure,
// no xdg-open at all, and files revealed by opening their folders. With
// the bus, on Wayland where there is one: a file shown by the file
// manager as a file URI, never by xdg-open; its folder opened when the
// file manager refuses; and a reveal superseded while the file manager
// answers, whose answer goes to no one. Either way, the preferred
// locales are the environment's.

#include "linux_bus_fake.h"
#include "test_harness.h"

#include "maul-window/event.h"
#include "maul-window/services.h"
#include "maul-window/system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 20000000000u

static char s_directory[] = "/tmp/mwin-services-XXXXXX";

typedef struct Step
{
    const char* what;
    mwinRequestKind kind;
    const char* text;
    // MWIN_EXIT for the stand-in, and whether the file manager refuses.
    const char* status;
    bool refuse;
    mwinOutcome outcome;
    // xdg-open's argument, and the item the file manager showed, or
    // NULL for none.
    const char* ran;
    const char* item;
} Step;

typedef struct Program
{
    const Step* steps;
    int count;
    int at;
    bool asked;
    bool superseding;
    mwinWindowId window;
    mwinRequestId create;
    mwinRequestId request;
    mwinRequestId superseded;
    int supersededOutcome;
    uint64_t startNs;
    FakeBus* fake;
    // The preferred locales at the start.
    char locales[32];
    size_t localeLength;
    bool done;
} Program;

static uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void Path(char* out, size_t size, const char* name)
{
    (void)snprintf(out, size, "%s/%s", s_directory, name);
}

static void Write(const char* name, const char* text, mode_t mode)
{
    char path[128];
    Path(path, sizeof(path), name);
    FILE* file = fopen(path, "w");
    (void)fputs(text, file);
    (void)fclose(file);
    (void)chmod(path, mode);
}

// Whether xdg-open ran with this argument, or did not run for NULL.
static bool Ran(const char* expected)
{
    char path[128];
    char text[512] = {0};
    Path(path, sizeof(path), "argument");
    FILE* file = fopen(path, "r");
    size_t length = file != nullptr ? fread(text, 1, sizeof(text) - 1, file) : 0;
    if (file != nullptr)
    {
        (void)fclose(file);
        (void)remove(path);
    }
    return expected == nullptr ? file == nullptr
                               : length > 0 && strncmp(text, expected, length - 1) == 0 &&
                                     strlen(expected) == length - 1;
}

static mwinResult Ask(mwinContext* context, Program* program, const Step* step,
                      mwinRequestId* request)
{
    size_t length = strlen(step->text);
    return step->kind == mwin_requestOpenUrl
               ? mwinRequestOpenUrl(context, program->window, step->text, length, request)
               : mwinRequestRevealFile(context, program->window, step->text, length, request);
}

static void Begin(mwinContext* context, Program* program)
{
    const Step* step = &program->steps[program->at];
    if (step->status != nullptr)
    {
        (void)setenv("MWIN_EXIT", step->status, 1);
    }
    else
    {
        (void)unsetenv("MWIN_EXIT");
    }
    if (program->fake != nullptr)
    {
        program->fake->refuseShow = step->refuse;
        program->fake->item[0] = '\0';
    }
    program->superseding = step->kind == mwin_requestRevealFile && program->fake != nullptr &&
                           !step->refuse && program->superseded.index1 == 0;
    if (program->superseding)
    {
        CHECK(Ask(context, program, step, &program->superseded) == mwin_success, "ask");
    }
    CHECK(Ask(context, program, step, &program->request) == mwin_success, step->what);
    program->asked = true;
}

static void Finish(Program* program, int outcome)
{
    const Step* step = &program->steps[program->at];
    bool shown = step->item == nullptr ||
                 (program->fake != nullptr && strcmp(program->fake->item, step->item) == 0 &&
                  program->fake->startup[0] == '\0');
    CHECK(outcome == step->outcome && Ran(step->ran) && shown, step->what);
    program->asked = false;
    program->at++;
}

static bool Same(mwinRequestId a, mwinRequestId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void Drain(mwinContext* context, Program* program)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        const mwinCompletion* completion = &event.data.completion;
        if (event.type != mwin_eventRequestCompleted)
        {
            continue;
        }
        if (Same(completion->request, program->create))
        {
            program->create = (mwinRequestId){0};
        }
        else if (Same(completion->request, program->superseded))
        {
            program->supersededOutcome = completion->outcome;
        }
        else if (program->asked && Same(completion->request, program->request))
        {
            Finish(program, completion->outcome);
        }
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    CHECK(mwinGetPreferredLocales(context, program->locales, sizeof(program->locales),
                                  &program->localeLength) == mwin_success,
          "locales");
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){200.0f, 100.0f};
    return mwinCreateWindow(context, &def, &program->window, &program->create);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    FakePump(program->fake);
    Drain(context, program);
    if (program->create.index1 == 0 && !program->asked && program->at < program->count)
    {
        Begin(context, program);
    }
    program->done = program->at == program->count;
    struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, nullptr);
    return program->done || NowNs() - program->startNs > DEADLINE_NS ? mwin_frameStop
                                                                     : mwin_frameContinue;
}

static void Run(const Step* steps, int count, FakeBus* fake, const char* what)
{
    static Program program;
    program = (Program){.steps = steps, .count = count, .fake = fake, .supersededOutcome = -1};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, what);
    CHECK(program.localeLength == 8 && memcmp(program.locales, "de-DE,en", 8) == 0,
          "the preferred locales the environment's");
    if (program.superseded.index1 != 0)
    {
        CHECK(program.supersededOutcome == mwin_outcomeSuperseded && fake->shown >= 2,
              "a reveal superseded while the file manager answers");
    }
}

static const Step s_withoutBus[] = {
    {"an address opened by xdg-open", mwin_requestOpenUrl, "https://example.com/?q=%C3%A9&x=1",
     nullptr, false, mwin_outcomeDone, "https://example.com/?q=%C3%A9&x=1", nullptr},
    {"no tool for the desktop unsupported", mwin_requestOpenUrl, "mailto:a@example.com", "3", false,
     mwin_outcomeUnsupported, "mailto:a@example.com", nullptr},
    {"xdg-open failing failed", mwin_requestOpenUrl, "http://example.com", "4", false,
     mwin_outcomeFailed, "http://example.com", nullptr},
    {"a file revealed by opening its folder", mwin_requestRevealFile, "/tmp/x y/z.txt", nullptr,
     false, mwin_outcomeDone, "/tmp/x y", nullptr},
    {"a file at the root revealed by opening the root", mwin_requestRevealFile, "/z.txt", nullptr,
     false, mwin_outcomeDone, "/", nullptr},
};

static const Step s_withBus[] = {
    {"a file shown by the file manager", mwin_requestRevealFile, "/tmp/a b/\xC3\xA9.txt", nullptr,
     false, mwin_outcomeDone, nullptr, "file:///tmp/a%20b/%C3%A9.txt"},
    {"its folder opened when the file manager refuses", mwin_requestRevealFile,
     "/tmp/a b/\xC3\xA9.txt", nullptr, true, mwin_outcomeDone, "/tmp/a b",
     "file:///tmp/a%20b/%C3%A9.txt"},
};

static const Step s_noOpener[] = {
    {"no xdg-open unsupported", mwin_requestOpenUrl, "http://example.com", nullptr, false,
     mwin_outcomeUnsupported, nullptr, nullptr},
};

static void RunWithBus(const char* wayland)
{
    static FakeBus fake;
    if (!FakeStart(&fake, s_directory))
    {
        (void)printf("no dbus-daemon or libdbus-1: the bus is not tested\n");
    }
    else
    {
        if (wayland != nullptr)
        {
            (void)setenv("WAYLAND_DISPLAY", wayland, 1);
        }
        Run(s_withBus, 2, &fake, "the services with a bus");
    }
    FakeStop(&fake);
}

int main(void)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || mkdtemp(s_directory) == nullptr)
    {
        return 77;
    }
    const char* wayland = getenv("WAYLAND_DISPLAY");
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    char saved[64] = {0};
    char savedRuntime[256] = {0};
    (void)snprintf(saved, sizeof(saved), "%s", wayland != nullptr ? wayland : "");
    (void)snprintf(savedRuntime, sizeof(savedRuntime), "%s", runtime != nullptr ? runtime : "");
    char script[256];
    (void)snprintf(
        script, sizeof(script),
        "#!/bin/sh\nprintf '%%s\\n' \"$1\" > \"%s/argument\"\nexit \"${MWIN_EXIT:-0}\"\n",
        s_directory);
    Write("xdg-open", script, 0700);
    char none[128];
    Path(none, sizeof(none), "none");
    (void)mkdir(none, 0700);
    (void)unsetenv("LC_ALL");
    (void)unsetenv("LC_MESSAGES");
    (void)setenv("LANG", "de_DE.UTF-8", 1);
    (void)setenv("LANGUAGE", "de_DE:en", 1);
    // No bus, and X11.
    (void)unsetenv("DBUS_SESSION_BUS_ADDRESS");
    (void)setenv("XDG_RUNTIME_DIR", s_directory, 1);
    (void)unsetenv("WAYLAND_DISPLAY");
    (void)setenv("PATH", s_directory, 1);
    Run(s_withoutBus, 5, nullptr, "the services without a bus");
    (void)setenv("PATH", none, 1);
    Run(s_noOpener, 1, nullptr, "the services without xdg-open");
    (void)setenv("PATH", s_directory, 1);
    if (savedRuntime[0] != '\0')
    {
        (void)setenv("XDG_RUNTIME_DIR", savedRuntime, 1);
    }
    RunWithBus(saved[0] != '\0' && savedRuntime[0] != '\0' ? saved : nullptr);
    char path[128];
    Path(path, sizeof(path), "xdg-open");
    (void)remove(path);
    (void)rmdir(none);
    FakeClean(s_directory);
    (void)rmdir(s_directory);
    return s_failures == 0 ? 0 : 1;
}
