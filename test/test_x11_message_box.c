// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box the library draws on an X server where neither zenity
// nor kdialog is on PATH, answered by the test's own connection as a
// user and a window manager would: Return accepts, closing the window
// refuses, a click on the first button accepts and on the second
// refuses, and the window is gone once answered. Needs an X server:
// skipped without DISPLAY.

#include "test_harness.h"

#include "maul-window/services.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

static char s_directory[] = "/tmp/mwin-x11-message-XXXXXX";
static xcb_connection_t* s_connection;
static xcb_window_t s_root;

typedef struct Shown
{
    mwinMessageBoxDef def;
    mwinResult result;
    bool accepted;
} Shown;

static void* Show(void* data)
{
    Shown* shown = data;
    shown->result = mwinShowMessageBox(&shown->def, &shown->accepted);
    return nullptr;
}

static xcb_atom_t Atom(const char* name)
{
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
        s_connection, xcb_intern_atom(s_connection, 0, (uint16_t)strlen(name), name), nullptr);
    xcb_atom_t atom = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

// The top-level window titled `title`, asking `tries` times 10 ms apart.
static xcb_window_t Titled(const char* title, int tries)
{
    for (int tried = 0; tried < tries; tried++)
    {
        xcb_query_tree_reply_t* tree =
            xcb_query_tree_reply(s_connection, xcb_query_tree(s_connection, s_root), nullptr);
        xcb_window_t found = XCB_WINDOW_NONE;
        const int count = tree != nullptr ? xcb_query_tree_children_length(tree) : 0;
        const xcb_window_t* children = tree != nullptr ? xcb_query_tree_children(tree) : nullptr;
        for (int i = 0; i < count && found == XCB_WINDOW_NONE; i++)
        {
            xcb_get_property_reply_t* name =
                xcb_get_property_reply(s_connection,
                                       xcb_get_property(s_connection, 0, children[i],
                                                        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 64),
                                       nullptr);
            if (name != nullptr && (size_t)xcb_get_property_value_length(name) == strlen(title) &&
                memcmp(xcb_get_property_value(name), title, strlen(title)) == 0)
            {
                xcb_get_window_attributes_reply_t* attributes = xcb_get_window_attributes_reply(
                    s_connection, xcb_get_window_attributes(s_connection, children[i]), nullptr);
                if (attributes != nullptr && attributes->map_state == XCB_MAP_STATE_VIEWABLE)
                {
                    found = children[i];
                }
                free(attributes);
            }
            free(name);
        }
        free(tree);
        if (found != XCB_WINDOW_NONE)
        {
            return found;
        }
        (void)nanosleep(&(struct timespec){.tv_nsec = 10000000}, nullptr);
    }
    return XCB_WINDOW_NONE;
}

static void Send(xcb_window_t window, const void* event, uint32_t mask)
{
    xcb_send_event(s_connection, 0, window, mask, event);
    xcb_flush(s_connection);
}

static void PressKey(xcb_window_t window, xcb_keycode_t key)
{
    xcb_key_press_event_t press = {
        .response_type = XCB_KEY_PRESS, .detail = key, .root = s_root, .event = window};
    char event[32] = {0};
    memcpy(event, &press, sizeof(press));
    Send(window, event, XCB_EVENT_MASK_KEY_PRESS);
}

// A left click `fromRight` and `fromBottom` pixels from the window's
// bottom right corner.
static void Click(xcb_window_t window, int fromRight, int fromBottom)
{
    xcb_get_geometry_reply_t* geometry =
        xcb_get_geometry_reply(s_connection, xcb_get_geometry(s_connection, window), nullptr);
    xcb_button_press_event_t press = {
        .response_type = XCB_BUTTON_PRESS, .detail = 1, .root = s_root, .event = window};
    if (geometry != nullptr)
    {
        press.event_x = (int16_t)(geometry->width - fromRight);
        press.event_y = (int16_t)(geometry->height - fromBottom);
    }
    free(geometry);
    char event[32] = {0};
    memcpy(event, &press, sizeof(press));
    Send(window, event, XCB_EVENT_MASK_BUTTON_PRESS);
}

static void Close(xcb_window_t window)
{
    xcb_client_message_event_t message = {.response_type = XCB_CLIENT_MESSAGE,
                                          .format = 32,
                                          .window = window,
                                          .type = Atom("WM_PROTOCOLS")};
    message.data.data32[0] = Atom("WM_DELETE_WINDOW");
    char event[32] = {0};
    memcpy(event, &message, sizeof(message));
    Send(window, event, XCB_EVENT_MASK_NO_EVENT);
}

// Whether the window titled `title` went within `tries` checks 10 ms
// apart. The box asks the X server to destroy its window and returns;
// requests of two connections are not ordered, so the server may not
// have done it when this connection first asks.
static bool Gone(const char* title, int tries)
{
    for (int tried = 0; tried < tries; tried++)
    {
        if (Titled(title, 1) == XCB_WINDOW_NONE)
        {
            return true;
        }
        (void)nanosleep(&(struct timespec){.tv_nsec = 10000000}, nullptr);
    }
    return false;
}

typedef enum Answer
{
    kReturn,
    kClose,
    kFirst,
    kSecond,
} Answer;

// Shows a box titled `title` with `buttons`, answers it as `answer`
// says, and gives what the box said; whether the window was found.
static bool Answered(const char* title, mwinMessageButtons buttons, Answer answer, Shown* shown)
{
    shown->def = mwinDefaultMessageBoxDef();
    shown->def.title = title;
    shown->def.titleLength = strlen(title);
    shown->def.message =
        "The graphics device could not be started: a long line that has to wrap at a "
        "space\nand a second line, caf\xC3\xA9 \xE2\x9C\x93";
    shown->def.messageLength = strlen(shown->def.message);
    shown->def.buttons = buttons;
    pthread_t thread;
    if (pthread_create(&thread, nullptr, Show, shown) != 0)
    {
        return false;
    }
    const xcb_window_t window = Titled(title, 500);
    if (window != XCB_WINDOW_NONE)
    {
        // The buttons, right to left from the bottom right corner: 16
        // pixels in, 68 wide with "fixed"'s six-pixel cells, 10 apart.
        switch (answer)
        {
        case kReturn:
            PressKey(window, 36);
            break;
        case kClose:
            Close(window);
            break;
        case kFirst:
            Click(window, buttons == mwin_buttonsOk ? 50 : 16 + 68 + 10 + 34, 28);
            break;
        case kSecond:
            Click(window, 50, 28);
            break;
        }
    }
    (void)pthread_join(thread, nullptr);
    return window != XCB_WINDOW_NONE && Gone(title, 100);
}

int main(void)
{
    if (getenv("DISPLAY") == nullptr || mkdtemp(s_directory) == nullptr)
    {
        return 77;
    }
    s_connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(s_connection) != 0)
    {
        return 77;
    }
    s_root = xcb_setup_roots_iterator(xcb_get_setup(s_connection)).data->root;
    // Neither zenity nor kdialog.
    setenv("PATH", s_directory, 1);
    Shown shown;
    CHECK(Answered("Maul one", mwin_buttonsOk, kReturn, &shown) && shown.result == mwin_success &&
              shown.accepted,
          "Return accepts, and the box is gone");
    CHECK(Answered("Maul two", mwin_buttonsYesNo, kClose, &shown) && shown.result == mwin_success &&
              !shown.accepted,
          "closing the window refuses");
    CHECK(Answered("Maul three", mwin_buttonsOkCancel, kFirst, &shown) &&
              shown.result == mwin_success && shown.accepted,
          "a click on OK accepts");
    CHECK(Answered("Maul four", mwin_buttonsYesNo, kSecond, &shown) &&
              shown.result == mwin_success && !shown.accepted,
          "a click on No refuses");
    xcb_disconnect(s_connection);
    (void)rmdir(s_directory);
    return s_failures == 0 ? 0 : 1;
}
