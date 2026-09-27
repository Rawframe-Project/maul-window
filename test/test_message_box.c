// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box on Linux against stand-ins for zenity and kdialog on
// PATH, scripts that write their arguments down and exit as told: the
// text as arguments, never through a shell; OK, Yes and No; an error
// of zenity's own; kdialog where zenity is missing; none where both
// are; and defs refused before anything runs.

#include "test_harness.h"

#include "maul-window/services.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char s_directory[] = "/tmp/mwin-message-XXXXXX";

// A stand-in for a program: its arguments one a line into a file, then
// the exit status MWIN_EXIT says.
static void StandIn(const char* name)
{
    char path[128];
    (void)snprintf(path, sizeof(path), "%s/%s", s_directory, name);
    FILE* file = fopen(path, "w");
    (void)fprintf(file,
                  "#!/bin/sh\nprintf '%%s\\n' \"$0\" \"$@\" > \"%s/arguments\"\n"
                  "exit \"${MWIN_EXIT:-0}\"\n",
                  s_directory);
    (void)fclose(file);
    (void)chmod(path, 0700);
}

// Whether the stand-in was run with these arguments, one a line.
static bool Ran(const char* expected)
{
    char path[128];
    char text[1024] = {0};
    (void)snprintf(path, sizeof(path), "%s/arguments", s_directory);
    FILE* file = fopen(path, "r");
    size_t length = file != nullptr ? fread(text, 1, sizeof(text) - 1, file) : 0;
    if (file != nullptr)
    {
        (void)fclose(file);
    }
    (void)remove(path);
    const char* name = strrchr(text, '/');
    return length > 0 && name != nullptr && strcmp(name + 1, expected) == 0;
}

static mwinMessageBoxDef Def(mwinMessageKind kind, mwinMessageButtons buttons)
{
    mwinMessageBoxDef def = mwinDefaultMessageBoxDef();
    def.title = "Maul";
    def.titleLength = 4;
    def.message = "It's $HOME; \xC3\xA9";
    def.messageLength = 14;
    def.kind = kind;
    def.buttons = buttons;
    return def;
}

static void CheckZenity(void)
{
    bool accepted = false;
    mwinMessageBoxDef info = Def(mwin_messageWarning, mwin_buttonsOk);
    CHECK(mwinShowMessageBox(&info, &accepted) == mwin_success && accepted &&
              Ran("zenity\n--warning\n--title=Maul\n--text=It's $HOME; \xC3\xA9\n--no-markup\n"),
          "zenity with the text as it is, and OK");
    mwinMessageBoxDef question = Def(mwin_messageInfo, mwin_buttonsYesNo);
    setenv("MWIN_EXIT", "1", 1);
    CHECK(mwinShowMessageBox(&question, &accepted) == mwin_success && !accepted &&
              Ran("zenity\n--question\n--title=Maul\n--text=It's $HOME; \xC3\xA9\n--no-markup\n"
                  "--ok-label=Yes\n--cancel-label=No\n"),
          "a question answered No");
    setenv("MWIN_EXIT", "5", 1);
    CHECK(mwinShowMessageBox(&question, &accepted) == mwin_errorPlatform, "an error of zenity's");
    unsetenv("MWIN_EXIT");
}

static void CheckKdialog(void)
{
    char path[128];
    (void)snprintf(path, sizeof(path), "%s/zenity", s_directory);
    (void)remove(path);
    StandIn("kdialog");
    bool accepted = false;
    mwinMessageBoxDef error = Def(mwin_messageError, mwin_buttonsOk);
    CHECK(mwinShowMessageBox(&error, &accepted) == mwin_success && accepted &&
              Ran("kdialog\n--title\nMaul\n--error\nIt's $HOME; \xC3\xA9\n"),
          "kdialog where zenity is missing");
    mwinMessageBoxDef question = Def(mwin_messageInfo, mwin_buttonsOkCancel);
    setenv("MWIN_EXIT", "2", 1);
    CHECK(mwinShowMessageBox(&question, &accepted) == mwin_success && !accepted &&
              Ran("kdialog\n--title\nMaul\n--yesno\nIt's $HOME; \xC3\xA9\n--yes-label\nOK\n"
                  "--no-label\nCancel\n"),
          "a question cancelled");
    unsetenv("MWIN_EXIT");
    (void)snprintf(path, sizeof(path), "%s/kdialog", s_directory);
    (void)remove(path);
    CHECK(mwinShowMessageBox(&question, &accepted) == mwin_errorUnsupported,
          "no message box without either");
}

static void CheckRefused(void)
{
    mwinMessageBoxDef def = Def(mwin_messageInfo, mwin_buttonsOk);
    mwinMessageBoxDef wrong = def;
    wrong.cookie = 0;
    mwinMessageBoxDef kind = def;
    kind.kind = 3;
    mwinMessageBoxDef text = def;
    text.message = "a\xC3";
    text.messageLength = 2;
    mwinMessageBoxDef nul = def;
    nul.message = "a\0b";
    nul.messageLength = 3;
    CHECK(mwinShowMessageBox(nullptr, nullptr) == mwin_errorInvalid &&
              mwinShowMessageBox(&wrong, nullptr) == mwin_errorInvalid &&
              mwinShowMessageBox(&kind, nullptr) == mwin_errorInvalid &&
              mwinShowMessageBox(&text, nullptr) == mwin_errorInvalid &&
              mwinShowMessageBox(&nul, nullptr) == mwin_errorInvalid && !Ran(""),
          "defs refused before anything runs");
}

int main(void)
{
    if (mkdtemp(s_directory) == nullptr)
    {
        return 77;
    }
    setenv("PATH", s_directory, 1);
    StandIn("zenity");
    CheckRefused();
    CheckZenity();
    CheckKdialog();
    (void)rmdir(s_directory);
    return s_failures == 0 ? 0 : 1;
}
