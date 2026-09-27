// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Linux file dialogs on X11 against stand-ins: the desktop portal on a
// bus of the test's own (linux_bus_fake.h), and zenity, a script on
// PATH that writes its arguments down, prints what it is told and ends
// as told. Through the portal: OpenFile over the window with its title,
// many files, the filters as case-blind globs, the first chosen, the
// folder as bytes, answered with file URIs; SaveFile with the name,
// closed by the user; a folder; a dialog closed when its window goes;
// a URI of no local file failed; zenity where the portal refuses.
// Without a bus, zenity: its arguments, a choice of several, closed,
// failing, past the limits, and missing. Skipped (exit status 77)
// without an X server.

#include "linux_bus_fake.h"
#include "test_harness.h"

#include "maul-window/dialog.h"
#include "maul-window/event.h"
#include "maul-window/native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEADLINE_NS 10000000000ull

static char s_directory[] = "/tmp/mwin-dialog-XXXXXX";

static const mwinFileFilter s_filters[] = {
    {"Images", 6, "png;JPG", 7},
    {"Text", 4, "txt", 3},
};

typedef struct Step
{
    const char* what;
    mwinDialogKind kind;
    // The portal's Response: its code and URIs; or, with refuse set or
    // no bus, zenity's output and status.
    int code;
    const char* const* uris;
    int uriCount;
    bool refuse;
    const char* output;
    const char* status;
    // How it ends, the paths it chose (NULL not to look), and what the
    // portal (with the parent as %s) or zenity was asked.
    mwinOutcome outcome;
    const char* files;
    size_t filesLength;
    const char* asked;
    // The window is destroyed while the portal shows the dialog.
    bool destroy;
} Step;

typedef struct Program
{
    FakeBus* fake;
    const Step* steps;
    int count;
    int at;
    bool asked;
    bool answered;
    int outcome;
    int chosen;
    mwinWindowId window;
    mwinRequestId create;
    mwinRequestId request;
    uint64_t startNs;
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

// Whether zenity ran with these arguments, one a line after its name.
static bool Ran(const char* expected)
{
    char path[128];
    char text[1024] = {0};
    Path(path, sizeof(path), "arguments");
    FILE* file = fopen(path, "r");
    size_t length = file != nullptr ? fread(text, 1, sizeof(text) - 1, file) : 0;
    if (file != nullptr)
    {
        (void)fclose(file);
        (void)remove(path);
    }
    const char* first = memchr(text, '\n', length);
    return first != nullptr && strcmp(first + 1, expected) == 0;
}

// Whether the portal was asked this, the window's parent put for %s.
static bool Asked(mwinContext* context, const Program* program, const char* expected)
{
    mwinNativeHandles handles;
    char parent[32] = "";
    char text[1024];
    if (mwinGetNativeHandles(context, program->window, &handles) == mwin_success)
    {
        (void)snprintf(parent, sizeof(parent), "x11:%x", (unsigned)handles.handles.x11.window);
    }
    (void)snprintf(text, sizeof(text), expected, parent);
    return strcmp(program->fake->chooser, text) == 0;
}

static mwinFileDialogDef Def(mwinDialogKind kind)
{
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = kind;
    def.title = "Pick";
    def.titleLength = 4;
    def.folder = "/tmp/a b";
    def.folderLength = 8;
    def.name = "out.txt";
    def.nameLength = 7;
    def.filters = s_filters;
    def.filterCount = 2;
    return def;
}

static mwinResult CreateWindow(mwinContext* context, Program* program)
{
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){200.0f, 100.0f};
    return mwinCreateWindow(context, &def, &program->window, &program->create);
}

static void Begin(mwinContext* context, Program* program)
{
    const Step* step = &program->steps[program->at];
    (void)setenv("MWIN_OUTPUT", step->output != nullptr ? step->output : "", 1);
    (void)setenv("MWIN_EXIT", step->status != nullptr ? step->status : "0", 1);
    if (program->fake != nullptr)
    {
        program->fake->refuseChooser = step->refuse;
        program->chosen = program->fake->chosen;
    }
    mwinFileDialogDef def = Def(step->kind);
    CHECK(mwinRequestFileDialog(context, program->window, &def, &program->request) == mwin_success,
          step->what);
    program->asked = true;
    program->answered = false;
    program->outcome = -1;
}

// Answers through the portal once it was asked, or ends the window.
static void Answer(mwinContext* context, Program* program)
{
    const Step* step = &program->steps[program->at];
    FakeBus* fake = program->fake;
    if (program->answered || fake == nullptr || step->refuse || fake->chosen == program->chosen)
    {
        return;
    }
    program->answered = true;
    if (step->destroy)
    {
        CHECK(mwinDestroyWindow(context, program->window) == mwin_success, "destroy");
    }
    else
    {
        FakeRespond(fake, (uint32_t)step->code, step->uris, step->uriCount);
    }
}

static bool Chose(mwinContext* context, const Program* program, const Step* step)
{
    char paths[256];
    size_t length = 0;
    return step->files == nullptr ||
           (mwinGetDialogFiles(context, program->request, paths, sizeof(paths), &length, nullptr) ==
                mwin_success &&
            length == step->filesLength && memcmp(paths, step->files, length) == 0);
}

// Ends a step once it completed, and its dialog closed if it should.
static void Finish(mwinContext* context, Program* program)
{
    const Step* step = &program->steps[program->at];
    if (program->outcome < 0 || (step->destroy && !program->fake->closed))
    {
        return;
    }
    bool portal = program->fake != nullptr && !step->refuse;
    bool asked = step->asked == nullptr ||
                 (portal ? Asked(context, program, step->asked) : Ran(step->asked));
    CHECK(program->outcome == step->outcome && Chose(context, program, step) && asked, step->what);
    program->asked = false;
    program->at++;
    if (step->destroy)
    {
        CHECK(CreateWindow(context, program) == mwin_success, "a window again");
    }
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
        if (completion->request.index1 == program->create.index1 &&
            completion->request.generation == program->create.generation)
        {
            program->create = (mwinRequestId){0};
        }
        else if (completion->request.index1 == program->request.index1 &&
                 completion->request.generation == program->request.generation)
        {
            program->outcome = completion->outcome;
        }
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    program->startNs = NowNs();
    return CreateWindow(context, program);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    FakePump(program->fake);
    Drain(context, program);
    if (program->asked)
    {
        Answer(context, program);
        Finish(context, program);
    }
    else if (program->create.index1 == 0 && program->at < program->count)
    {
        Begin(context, program);
    }
    program->done = program->at == program->count;
    struct timespec pause = {0, 1000000};
    (void)nanosleep(&pause, nullptr);
    return program->done || NowNs() - program->startNs > DEADLINE_NS ? mwin_frameStop
                                                                     : mwin_frameContinue;
}

static void Run(const Step* steps, int count, FakeBus* fake, uint32_t dialogBytes, const char* what)
{
    static Program program;
    program = (Program){.steps = steps, .count = count, .fake = fake};
    mwinAppDef def = mwinDefaultAppDef();
    def.context.limits.dialogBytes = dialogBytes;
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    CHECK(mwinRun(&def) == mwin_success && program.done, what);
}

#define FILTERS                                                                                    \
    "filters=Images:*.[pP][nN][gG],*.[jJ][pP][gG],|Text:*.[tT][xX][tT],|;"                         \
    "current_filter=Images:*.[pP][nN][gG],*.[jJ][pP][gG],;"
#define ZENITY_FILTERS                                                                             \
    "--file-filter=Images | *.[pP][nN][gG] *.[jJ][pP][gG]\n--file-filter=Text | *.[tT][xX][tT]\n"

static const char* const s_chosen[] = {"file:///tmp/a%20b/x.png", "file:///tmp/%C3%A9.JPG"};
static const char* const s_folder[] = {"file:///tmp"};
static const char* const s_web[] = {"https://example.com/x"};

static const Step s_portal[] = {
    {.what = "many files through the portal, over the window, with the filters and folder",
     .kind = mwin_dialogOpenMany,
     .uris = s_chosen,
     .uriCount = 2,
     .outcome = mwin_outcomeDone,
     .files = "/tmp/a b/x.png\0/tmp/\xC3\xA9.JPG",
     .filesLength = 27,
     .asked = "OpenFile;parent=%s;title=Pick;multiple=true;" FILTERS "current_folder=/tmp/a b;"},
    {.what = "a save closed by the user",
     .kind = mwin_dialogSave,
     .code = 1,
     .outcome = mwin_outcomeCancelled,
     .asked =
         "SaveFile;parent=%s;title=Pick;" FILTERS "current_folder=/tmp/a b;current_name=out.txt;"},
    {.what = "a folder",
     .kind = mwin_dialogFolder,
     .uris = s_folder,
     .uriCount = 1,
     .outcome = mwin_outcomeDone,
     .files = "/tmp",
     .filesLength = 5,
     .asked = "OpenFile;parent=%s;title=Pick;directory=true;current_folder=/tmp/a b;"},
    {.what = "a dialog closed when its window goes",
     .kind = mwin_dialogOpen,
     .outcome = mwin_outcomeCancelled,
     .destroy = true,
     .asked = nullptr},
    {.what = "a URI of no local file failed",
     .kind = mwin_dialogOpen,
     .uris = s_web,
     .uriCount = 1,
     .outcome = mwin_outcomeFailed,
     .asked = nullptr},
    {.what = "zenity where the portal refuses",
     .kind = mwin_dialogOpen,
     .refuse = true,
     .output = "/z\n",
     .outcome = mwin_outcomeDone,
     .files = "/z",
     .filesLength = 3,
     .asked = "--file-selection\n--title=Pick\n--filename=/tmp/a b/\n" ZENITY_FILTERS},
};

static const Step s_zenity[] = {
    {.what = "several files by zenity",
     .kind = mwin_dialogOpenMany,
     .output = "/a\x1e/b c\n",
     .outcome = mwin_outcomeDone,
     .files = "/a\0/b c",
     .filesLength = 8,
     .asked = "--file-selection\n--title=Pick\n--multiple\n--separator=\x1e\n"
              "--filename=/tmp/a b/\n" ZENITY_FILTERS},
    {.what = "a save closed in zenity",
     .kind = mwin_dialogSave,
     .status = "1",
     .outcome = mwin_outcomeCancelled,
     .asked =
         "--file-selection\n--title=Pick\n--save\n--filename=/tmp/a b/out.txt\n" ZENITY_FILTERS},
    {.what = "zenity failing failed",
     .kind = mwin_dialogFolder,
     .status = "5",
     .outcome = mwin_outcomeFailed,
     .asked = "--file-selection\n--title=Pick\n--directory\n--filename=/tmp/a b/\n"},
    {.what = "a choice past the limits too large",
     .kind = mwin_dialogOpen,
     .output = "/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n",
     .outcome = mwin_outcomeTooLarge,
     .asked = "--file-selection\n--title=Pick\n--filename=/tmp/a b/\n" ZENITY_FILTERS},
};

static const Step s_none[] = {
    {.what = "no zenity unsupported",
     .kind = mwin_dialogOpen,
     .outcome = mwin_outcomeUnsupported,
     .asked = nullptr},
};

int main(void)
{
    if (getenv("DISPLAY") == nullptr || mkdtemp(s_directory) == nullptr)
    {
        return 77;
    }
    (void)unsetenv("WAYLAND_DISPLAY");
    char script[256];
    (void)snprintf(script, sizeof(script),
                   "#!/bin/sh\nprintf '%%s\\n' \"$0\" \"$@\" > \"%s/arguments\"\n"
                   "printf '%%s' \"$MWIN_OUTPUT\"\nexit \"${MWIN_EXIT:-0}\"\n",
                   s_directory);
    char path[128];
    Path(path, sizeof(path), "zenity");
    FILE* file = fopen(path, "w");
    (void)fputs(script, file);
    (void)fclose(file);
    (void)chmod(path, 0700);
    char none[128];
    Path(none, sizeof(none), "none");
    (void)mkdir(none, 0700);
    (void)setenv("PATH", s_directory, 1);
    static FakeBus fake;
    if (FakeStart(&fake, s_directory))
    {
        Run(s_portal, 6, &fake, 1u << 20, "dialogs through the portal");
    }
    else
    {
        (void)printf("no dbus-daemon or libdbus-1: the portal is not tested\n");
    }
    FakeStop(&fake);
    (void)setenv("XDG_RUNTIME_DIR", s_directory, 1);
    Run(s_zenity, 4, nullptr, 32, "dialogs by zenity");
    (void)setenv("PATH", none, 1);
    Run(s_none, 1, nullptr, 1u << 20, "dialogs without zenity");
    (void)remove(path);
    (void)rmdir(none);
    FakeClean(s_directory);
    (void)rmdir(s_directory);
    return s_failures == 0 ? 0 : 1;
}
