# mwin-0015. Owned and popup windows

Status: Accepted

## Context

The requirements (section 4) list dialogs, menus and tooltips among
the windows a program makes. Every desktop keeps a dialog in front of
the window it belongs to, and lets a menu or a tooltip be placed
against a window's content without a frame. The platforms differ in who
places them: Win32 and X11 let the program place a popup anywhere on
the desktop, and Wayland lets the compositor place it against its
parent, sliding or flipping it onto the output. Wayland also dismisses
a menu when the user clicks elsewhere, and it gives no window a place
on the desktop.

## Decision

- **Part of the window's def:**
  - `owner` names the window this one belongs to, or none.
  - `kind` is one of:
    - `mwin_windowNormal`, which is a dialog when it has an owner.
    - `mwin_windowMenu`, a popup that takes the keyboard.
    - `mwin_windowTooltip`, a popup that never takes the keyboard.
  - `position` places a popup, in logical units from the top left of
    its owner's client area.
- **Def rules:**
  - A popup needs an owner, windowed mode and a finite position.
  - An owner that no longer exists is `mwin_errorStale`.
- **Popup requests and state:**
  - A popup is windowed and undecorated on every platform.
  - The core answers its mode and style requests with
    `mwin_outcomeUnsupported` before any backend sees them.
  - Its position, in its state and its `mwin_eventMoved`, is against its
    owner as well, and it stays there as the owner moves.
- **Destroying:** destroying a window first destroys the windows it
  owns, theirs before them. No platform is ever left with a popup whose
  parent is gone.
- **Dismissal:** a menu is dismissed when the keyboard goes elsewhere,
  which is how Wayland ends its grab. Every platform reports this as
  `mwin_eventCloseRequested`, and the program decides whether to destroy
  the menu.
- **Win32:**
  - An owned window is created with its owner's window as the owner and
    without `WS_EX_APPWINDOW`, so it stays off the taskbar.
  - A popup is `WS_POPUP` with `WS_EX_TOOLWINDOW`. A tooltip adds
    `WS_EX_NOACTIVATE` and `WS_EX_TOPMOST` and is shown without
    activation.
  - A popup is placed at its owner's client origin plus its offset in
    the owner's pixels. When the owner moves, its popups move with it,
    and a popup reports a move only when its offset changed.
  - A menu that loses the keyboard asks to close.
- **X11:**
  - An owned window gets `WM_TRANSIENT_FOR` and
    `_NET_WM_WINDOW_TYPE_DIALOG`.
  - A popup is override-redirect, is transient for its owner, and has
    the popup menu or tooltip window type. It is placed and follows its
    owner without the window manager.
  - A menu takes the input focus once it is mapped. When the focus
    leaves, it asks to close.
  - A tooltip's focus requests are denied.
- **Wayland:**
  - An owned window's toplevel has its owner's toplevel as its parent.
  - A popup is an `xdg_popup` of its owner's `xdg_surface`, with a
    positioner:
    - The anchor is the top left of the owner's window geometry. The
      protocol forbids an anchor rectangle outside that geometry, but
      an offset may leave it.
    - The offset is the popup's place plus the owner's caption.
    - The compositor may slide or flip the popup onto the output.
    - The positioner is reactive from xdg_wm_base version 3.
  - The popup's configure tells where the compositor put it, and that
    is what the program is told.
  - A menu grabs the seat with the latest input's serial. Without one
    it shows without the keyboard, because the compositor would dismiss
    it at once. `popup_done` is the close request.
  - Moves and resizes reposition the popup, from xdg_wm_base version 3.
    Before that, they are unsupported.
  - A popup draws no frame.
  - A popup's title is kept for the program. Its size limits and icon
    are unsupported.
- **The web:** a page has no windows over its canvases, so creating a
  popup is unsupported.

## Consequences

The contract tests run on the test backend and cover:

- the defs that are refused,
- a popup's reported place,
- the unsupported modes and styles,
- the order of destruction.

Each backend is tested against the platform's own reading:

- Win32 reads back owners, styles, activation and client origins.
- X11 reads the properties, override-redirect, the focus and origins
  from another client, and relies on the X server's event order to
  count moves.
- The Wayland test brings an xdg shell of its own to the test
  compositor. It records the parents, the positioners, the grabs, the
  repositions and the order in which roles went. Its compositor slides
  every popup a pixel, so the reported place is the compositor's and
  not the one asked for.
