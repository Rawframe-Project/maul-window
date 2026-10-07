// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The file dialog contract on the test backend: defs refused at the
// call (a wrong cookie or kind, text that is not UTF-8, a folder that
// is not absolute, filters without a name, with a dot, a wildcard, an
// empty extension, or too many, while the most are taken, and a
// share's folder outside Windows); a dialog's def copied as asked, its
// paths copied out under its request and stale under any other; a
// cancelled dialog, one past the limits and one choosing nothing,
// which keep the last paths; a folder dialog without filters, whose
// paths replace them; and dialogs cancelled with their window or never
// answered, their defs given back (the sanitizer builds would report a
// leak).

#include "test_program.h"

#include "maul-window/dialog.h"
#include "maul-window/test.h"

#include <string.h>

#ifdef _WIN32
#define ROOT "C:"
#else
#define ROOT ""
#endif

static const mwinFileFilter s_filters[] = {
    {"Images", 6, "png;jpg", 7},
    {"Text", 4, "txt", 3},
};

static const char s_chosen[] = ROOT "/a.png\0" ROOT "/b \xC3\xA9.jpg";

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

static mwinFileDialogDef Def(mwinDialogKind kind)
{
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = kind;
    def.title = "\xC3\x96"
                "ffnen";
    def.titleLength = 7;
    def.folder = ROOT "/tmp";
    def.folderLength = sizeof(ROOT "/tmp") - 1;
    def.filters = s_filters;
    def.filterCount = 2;
    return def;
}

static bool Refused(mwinContext* context, mwinWindowId window, const mwinFileDialogDef* def)
{
    return mwinRequestFileDialog(context, window, def, nullptr) == mwin_errorInvalid;
}

// A def with one filter of these extensions.
static bool RefusesExtensions(mwinContext* context, mwinWindowId window, const char* extensions)
{
    mwinFileFilter filter = {"Any", 3, extensions, strlen(extensions)};
    mwinFileDialogDef def = Def(mwin_dialogOpen);
    def.filters = &filter;
    def.filterCount = 1;
    return Refused(context, window, &def);
}

static void CheckRefusals(mwinContext* context, mwinWindowId window)
{
    mwinFileDialogDef cookie = Def(mwin_dialogOpen);
    cookie.cookie = 0;
    mwinFileDialogDef kind = Def(4);
    mwinFileDialogDef title = Def(mwin_dialogOpen);
    title.title = "a\xC3";
    title.titleLength = 2;
    mwinFileDialogDef folder = Def(mwin_dialogOpen);
    folder.folder = "tmp";
    folder.folderLength = 3;
    mwinFileDialogDef many = Def(mwin_dialogOpen);
    many.filterCount = MWIN_DIALOG_FILTERS + 1;
    mwinFileDialogDef none = Def(mwin_dialogOpen);
    none.filters = nullptr;
    mwinFileFilter nameless = {"", 0, "png", 3};
    mwinFileDialogDef unnamed = Def(mwin_dialogOpen);
    unnamed.filters = &nameless;
    unnamed.filterCount = 1;
    CHECK(Refused(context, window, nullptr) && Refused(context, window, &cookie) &&
              Refused(context, window, &kind) && Refused(context, window, &title) &&
              Refused(context, window, &folder) && Refused(context, window, &many) &&
              Refused(context, window, &none) && Refused(context, window, &unnamed) &&
              mwinRequestFileDialog(nullptr, window, &cookie, nullptr) == mwin_errorInvalid,
          "defs refused at the call");
    CHECK(RefusesExtensions(context, window, "") && RefusesExtensions(context, window, ".png") &&
              RefusesExtensions(context, window, "*.png") &&
              RefusesExtensions(context, window, "png;") &&
              RefusesExtensions(context, window, "png;;jpg") &&
              RefusesExtensions(context, window, "p?g") &&
              RefusesExtensions(context, window, "a b"),
          "extensions with a dot, a wildcard, a space or an empty one refused");
    // The most filters, with a share's folder where Windows names one;
    // the dialog asked for next supersedes this one.
    mwinFileFilter filters[MWIN_DIALOG_FILTERS];
    for (int i = 0; i < MWIN_DIALOG_FILTERS; i++)
    {
        filters[i] = s_filters[1];
    }
    mwinFileDialogDef most = Def(mwin_dialogOpenMany);
    most.filters = filters;
    most.filterCount = MWIN_DIALOG_FILTERS;
    mwinFileDialogDef share = Def(mwin_dialogOpenMany);
    share.folder = "\\\\server\\share";
    share.folderLength = 14;
#ifdef _WIN32
    most.folder = share.folder;
    most.folderLength = share.folderLength;
#else
    CHECK(Refused(context, window, &share), "a share's path is not absolute here");
#endif
    CHECK(mwinRequestFileDialog(context, window, &most, nullptr) == mwin_success,
          "the most filters");
}

static bool Described(mwinContext* context, const char* expected)
{
    char text[256];
    size_t length = 0;
    return mwinTestGetDialog(context, text, sizeof(text), &length) == mwin_success &&
           length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static bool Chose(mwinContext* context, mwinRequestId request, const char* expected,
                  size_t expectedLength, uint32_t expectedCount)
{
    char paths[64];
    size_t length = 0;
    uint32_t count = 0;
    return mwinGetDialogFiles(context, request, paths, sizeof(paths), &length, &count) ==
               mwin_success &&
           length == expectedLength && count == expectedCount &&
           memcmp(paths, expected, length) == 0;
}

static bool Stale(mwinContext* context, mwinRequestId request)
{
    size_t length = 0;
    return mwinGetDialogFiles(context, request, nullptr, 0, &length, nullptr) == mwin_errorStale;
}

static void Ask(Program* program, mwinContext* context, int request, mwinDialogKind kind)
{
    mwinFileDialogDef def = Def(kind);
    def.name = "x.txt";
    def.nameLength = 5;
    CHECK(mwinRequestFileDialog(context, program->windows[0], &def, &program->requests[request]) ==
              mwin_success,
          "ask for a dialog");
}

static void CheckChosen(Program* program, mwinContext* context)
{
    CHECK(Outcome(program, 1) == mwin_outcomeDone &&
              Described(context, "1\n\xC3\x96"
                                 "ffnen\n" ROOT "/tmp\nx.txt\nImages:png;jpg\nText:txt\n"),
          "a dialog's def copied as asked");
    size_t length = 0;
    char small[4];
    CHECK(Chose(context, program->requests[1], s_chosen, sizeof(s_chosen), 2) &&
              mwinGetDialogFiles(context, program->requests[1], small, sizeof(small), &length,
                                 nullptr) == mwin_errorCapacity &&
              length == sizeof(s_chosen) && memcmp(small, s_chosen, sizeof(small)) == 0 &&
              Stale(context, program->requests[0]) && Stale(context, (mwinRequestId){0}),
          "its paths copied out under its request, stale under another");
    CHECK(mwinTestSetAnswer(context, mwin_requestFileDialog, mwin_outcomeCancelled) == mwin_success,
          "cancel the next");
    Ask(program, context, 2, mwin_dialogSave);
}

static void CheckUnchosen(Program* program, mwinContext* context, int step)
{
    static const int outcomes[] = {mwin_outcomeCancelled, mwin_outcomeTooLarge, mwin_outcomeFailed};
    static const char* const what[] = {"a cancelled dialog keeps the last paths",
                                       "a choice past the limits too large",
                                       "a choice of nothing failed"};
    int at = step - 3;
    CHECK(Outcome(program, 2 + at) == outcomes[at] &&
              Chose(context, program->requests[1], s_chosen, sizeof(s_chosen), 2) &&
              Stale(context, program->requests[2 + at]),
          what[at]);
    CHECK(mwinTestSetAnswer(context, mwin_requestFileDialog, mwin_outcomeDone) == mwin_success,
          "answer the next");
    if (at == 0)
    {
        static const char three[] = "/a\0/b\0/c";
        CHECK(mwinTestSetDialogFiles(context, three, sizeof(three)) == mwin_success, "three");
        Ask(program, context, 3, mwin_dialogOpenMany);
    }
    else if (at == 1)
    {
        CHECK(mwinTestSetDialogFiles(context, nullptr, 0) == mwin_success, "none");
        Ask(program, context, 4, mwin_dialogOpen);
    }
    else
    {
        CHECK(mwinTestSetDialogFiles(context, ROOT "/tmp", sizeof(ROOT "/tmp")) == mwin_success,
              "a folder");
        Ask(program, context, 5, mwin_dialogFolder);
    }
}

static void CheckFolder(Program* program, mwinContext* context)
{
    CHECK(Outcome(program, 5) == mwin_outcomeDone &&
              Described(context, "3\n\xC3\x96"
                                 "ffnen\n" ROOT "/tmp\nx.txt\n") &&
              Chose(context, program->requests[5], ROOT "/tmp", sizeof(ROOT "/tmp"), 1) &&
              Stale(context, program->requests[1]),
          "a folder dialog without filters, whose paths replace the last");
    // Held: one dialog is cancelled with its window, one never answered.
    program->windows[1] = Create(context, nullptr);
    mwinFileDialogDef def = Def(mwin_dialogOpen);
    CHECK(mwinTestHold(context, true) == mwin_success &&
              mwinRequestFileDialog(context, program->windows[0], &def, &program->requests[6]) ==
                  mwin_success &&
              mwinRequestFileDialog(context, program->windows[1], &def, nullptr) == mwin_success &&
              mwinDestroyWindow(context, program->windows[0]) == mwin_success,
          "dialogs held, one window destroyed");
}

static void Step(Program* program, mwinContext* context, int step)
{
    Drain(program, context);
    switch (step)
    {
    case 0:
        program->windows[0] = Create(context, nullptr);
        break;
    case 1:
        CheckRefusals(context, program->windows[0]);
        CHECK(mwinTestSetDialogFiles(context, s_chosen, sizeof(s_chosen)) == mwin_success,
              "the paths chosen");
        Ask(program, context, 1, mwin_dialogOpenMany);
        break;
    case 2:
        CheckChosen(program, context);
        break;
    case 3:
    case 4:
    case 5:
        CheckUnchosen(program, context, step);
        break;
    case 6:
        CheckFolder(program, context);
        break;
    default:
        CHECK(Outcome(program, 6) == mwin_outcomeCancelled, "cancelled with its window");
        program->done = true;
        break;
    }
}

int main(void)
{
    Program program = {.step = Step};
    mwinContextDef def = mwinDefaultContextDef();
    def.limits.dialogFiles = 2;
    CHECK(RunWith(&program, def) == mwin_success && program.done, "the program runs");
    return s_failures == 0 ? 0 : 1;
}
