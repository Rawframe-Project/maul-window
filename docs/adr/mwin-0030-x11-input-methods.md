# mwin-0030. Input methods on X11

Status: Accepted

## Context

The requirements ask for an input method on every desktop, and name
"XIM/IBus" for X11. The X11 backend had text from the keymap and
compose sequences only, so a user typing Chinese, Japanese or Korean
through an input method got nothing. XIM, the X11 protocol, has its
only complete client in Xlib, which the backend does not use; a client
on XCB means speaking the protocol in the library, or taking
third-party LGPL code (xcb-imdkit), and on-the-spot preedit through XIM
depends on the server's callbacks style. The frameworks X11 desktops
run, IBus (GNOME, Ubuntu, Fedora) and Fcitx 5 (KDE, many CJK users),
both serve a D-Bus interface on the session bus with on-the-spot
preedit, and the library already reaches the session bus
asynchronously, with libdbus loaded at run time. SDL 3 takes the same
two, calling `ProcessKeyEvent` blocking; GTK's and Qt's modules call it
asynchronously and hold keys until the answer.

## Decision

- **The D-Bus frameworks, no XIM.** The X11 backend speaks Fcitx 5
  (`org.freedesktop.portal.Fcitx`) and IBus through its portal
  (`org.freedesktop.portal.IBus`). Fcitx comes first when `XMODIFIERS`
  names it, IBus first otherwise, and the other is tried when the first
  makes no input context. Neither found leaves keymap text as before.
- **One context, following the focus.** The backend makes one input
  context and gives it the focus while a focused window accepts text
  (`mwinRequestTextInput`), with the caret in root coordinates; when
  that ends it resets the context and takes the focus away. It asks for
  preedit the program draws (Fcitx's formatted preedit, IBus's preedit
  text).
- **Keys held, never lost.** While the context has the focus each key
  goes to the method without waiting, and it and every key after it are
  held in order (at most 32) until its answer: a key the method took is
  dropped, and so is the release of a press it took; a key it left, or
  whose answer takes more than 100 ms, is posted as the backend made it,
  with its keymap text. A stuck method costs a delay, never a key.
- **Text and compositions.** Committed text arrives as
  `mwin_eventTextInput`, compositions as `mwin_eventImePreedit` with
  their caret and segments (Fcitx's highlight, IBus's background
  attribute, as the target), repaired to UTF-8 like any platform text.

## Consequences

CJK input works on X11 desktops running IBus 1.5.17 or later, or Fcitx
5, including X11 programs under XWayland, with nothing new to link.
Older frameworks reachable only through XIM (uim, kinput2) are not
served. The test runs stand-ins for both frameworks on a private
session bus, so CI checks the protocol without either installed.
