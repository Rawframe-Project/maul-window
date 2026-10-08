// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box on Win32, answered from a second thread as a user
// would: its title and message as given (outside ASCII too, and of the
// longest a def holds), the buttons its def asks for and no others; OK
// and Yes accepted, Cancel and No not.

#include "test_harness.h"

#include "maul-window/services.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEADLINE_MS 10000u

// What the second thread looks for and presses, and what it found.
typedef struct Box
{
    const WCHAR* title;
    const WCHAR* message;
    int buttons[2];
    int press;
    DWORD thread;
    bool shown;
} Box;

static BOOL CALLBACK FindBox(HWND window, LPARAM data)
{
    WCHAR name[8];
    if (GetClassNameW(window, name, 8) > 0 && wcscmp(name, L"#32770") == 0)
    {
        *(HWND*)data = window;
        return FALSE;
    }
    return TRUE;
}

// Whether the box is the one asked for: its title, its message and the
// buttons, each and only these of OK, Cancel, Yes and No.
static bool IsAsked(HWND box, const Box* asked)
{
    static WCHAR title[MWIN_MESSAGE_TITLE_BYTES + 1];
    static const int every[] = {IDOK, IDCANCEL, IDYES, IDNO};
    if (GetWindowTextW(box, title, MWIN_MESSAGE_TITLE_BYTES + 1) == 0 ||
        wcscmp(title, asked->title) != 0 ||
        FindWindowExW(box, nullptr, L"Static", asked->message) == nullptr)
    {
        return false;
    }
    int buttons = 0;
    bool unwanted = false;
    for (int i = 0; i < 4; i++)
    {
        bool shown = GetDlgItem(box, every[i]) != nullptr;
        bool wanted = every[i] == asked->buttons[0] || every[i] == asked->buttons[1];
        buttons += shown ? 1 : 0;
        unwanted |= shown && !wanted;
    }
    // A box of OK alone has one button, whose id is IDOK under wine and
    // IDCANCEL on Windows: Escape answers it.
    if (asked->buttons[0] == asked->buttons[1])
    {
        return buttons == 1 && GetDlgItem(box, IDYES) == nullptr &&
               GetDlgItem(box, IDNO) == nullptr;
    }
    return buttons == 2 && !unwanted;
}

// The button to press: OK alone may carry IDCANCEL.
static HWND ButtonOf(HWND box, const Box* asked)
{
    HWND button = GetDlgItem(box, asked->press);
    return button == nullptr && asked->buttons[0] == asked->buttons[1] ? GetDlgItem(box, IDCANCEL)
                                                                       : button;
}

// Prints what the box shows where it is not the one asked for.
static void Describe(HWND box)
{
    static WCHAR title[MWIN_MESSAGE_TITLE_BYTES + 1];
    (void)GetWindowTextW(box, title, MWIN_MESSAGE_TITLE_BYTES + 1);
    (void)printf("box \"%.40ls\":", title);
    for (HWND child = GetWindow(box, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT))
    {
        WCHAR name[32];
        WCHAR text[48];
        (void)GetClassNameW(child, name, 32);
        (void)GetWindowTextW(child, text, 48);
        (void)printf(" [%ls %d \"%.40ls\"]", name, GetDlgCtrlID(child), text);
    }
    (void)printf("\n");
}

// Waits for the box, notes whether it is the one asked for, and presses
// the button; a box that never shows or does not close ends the test,
// which would wait on it forever.
static DWORD WINAPI Answer(void* data)
{
    Box* asked = data;
    ULONGLONG startMs = GetTickCount64();
    HWND box = nullptr;
    while (box == nullptr && GetTickCount64() - startMs < DEADLINE_MS)
    {
        Sleep(20);
        EnumThreadWindows(asked->thread, FindBox, (LPARAM)&box);
    }
    if (box == nullptr)
    {
        (void)printf("FAIL: no box shown\n");
        ExitProcess(1);
    }
    // Its controls come with it, but a frame may pass before they are all
    // there.
    while (!IsAsked(box, asked) && GetTickCount64() - startMs < DEADLINE_MS)
    {
        Sleep(20);
    }
    asked->shown = IsAsked(box, asked);
    if (!asked->shown)
    {
        Describe(box);
    }
    HWND button = ButtonOf(box, asked);
    if (button != nullptr)
    {
        (void)PostMessageW(button, BM_CLICK, 0, 0);
    }
    else
    {
        (void)PostMessageW(box, WM_CLOSE, 0, 0);
    }
    startMs = GetTickCount64();
    while (IsWindow(box) && GetTickCount64() - startMs < DEADLINE_MS)
    {
        Sleep(20);
    }
    if (IsWindow(box))
    {
        (void)printf("FAIL: the box did not close\n");
        ExitProcess(1);
    }
    return 0;
}

// Shows the box, the second thread answering it: whether it was shown as
// asked and accepted as expected.
static bool Shows(const char* title, const char* message, mwinMessageButtons buttons, Box* asked,
                  bool accepted)
{
    mwinMessageBoxDef def = mwinDefaultMessageBoxDef();
    def.title = title;
    def.titleLength = strlen(title);
    def.message = message;
    def.messageLength = strlen(message);
    def.kind = mwin_messageWarning;
    def.buttons = buttons;
    asked->thread = GetCurrentThreadId();
    HANDLE thread = CreateThread(nullptr, 0, Answer, asked, 0, nullptr);
    bool answered = !accepted;
    mwinResult result = mwinShowMessageBox(&def, &answered);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return result == mwin_success && asked->shown && answered == accepted;
}

int main(void)
{
    Box ok = {L"Saved", L"Café ☃", {IDOK, IDOK}, IDOK, 0, false};
    CHECK(Shows("Saved", "Caf\xC3\xA9 \xE2\x98\x83", mwin_buttonsOk, &ok, true),
          "OK alone, the text outside ASCII as given, accepted");
    Box cancel = {L"Quit", L"Leave?", {IDOK, IDCANCEL}, IDCANCEL, 0, false};
    CHECK(Shows("Quit", "Leave?", mwin_buttonsOkCancel, &cancel, false), "Cancel not accepted");
    Box okay = {L"Quit", L"Leave?", {IDOK, IDCANCEL}, IDOK, 0, false};
    CHECK(Shows("Quit", "Leave?", mwin_buttonsOkCancel, &okay, true), "OK beside Cancel accepted");
    Box yes = {L"Keep", L"Keep it?", {IDYES, IDNO}, IDYES, 0, false};
    CHECK(Shows("Keep", "Keep it?", mwin_buttonsYesNo, &yes, true), "Yes accepted");
    Box no = {L"Keep", L"Keep it?", {IDYES, IDNO}, IDNO, 0, false};
    CHECK(Shows("Keep", "Keep it?", mwin_buttonsYesNo, &no, false), "No not accepted");
    // The longest title and message a def holds, in ASCII, shown whole.
    static char title[MWIN_MESSAGE_TITLE_BYTES + 1];
    static char message[MWIN_MESSAGE_BYTES + 1];
    static WCHAR wideTitle[MWIN_MESSAGE_TITLE_BYTES + 1];
    static WCHAR wideMessage[MWIN_MESSAGE_BYTES + 1];
    memset(title, 't', MWIN_MESSAGE_TITLE_BYTES);
    memset(message, 'm', MWIN_MESSAGE_BYTES);
    wmemset(wideTitle, L't', MWIN_MESSAGE_TITLE_BYTES);
    wmemset(wideMessage, L'm', MWIN_MESSAGE_BYTES);
    Box longest = {wideTitle, wideMessage, {IDOK, IDOK}, IDOK, 0, false};
    CHECK(Shows(title, message, mwin_buttonsOk, &longest, true),
          "the longest title and message whole");
    return s_failures == 0 ? 0 : 1;
}
