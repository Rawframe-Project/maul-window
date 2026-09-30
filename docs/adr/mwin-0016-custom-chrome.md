# mwin-0016. Custom chrome

Status: Accepted

## Context

The requirements (section 4) ask for undecorated windows whose chrome
the program draws. The program declares typed hit regions (caption,
each edge and corner, client), which the platform uses for dragging and
resizing. The regions are data and never a synchronous callback (family 0018).
Windows 11 snap layouts over a custom maximize button are a named
optional capability.

The platforms differ in who carries out a drag:

- **Windows** asks the window which part of its frame a point is, with
  `WM_NCHITTEST`, and then runs the move or resize itself. It shows snap
  layouts over whatever answers `HTMAXBUTTON`.
- **X11 and Wayland** leave the client area to the program. The program
  asks the window manager (`_NET_WM_MOVERESIZE`) or the compositor
  (`xdg_toplevel.move` and `resize`) at a press.

## Decision

- **Style:** `mwin_styleCustomChrome` means the platform draws no title
  bar or border, and this overrides `mwin_styleDecorated`. The platform
  keeps what its frame does (shadow, snapping, animations) wherever it
  can.
- **The request:**
  - `mwinRequestHitRegions` sends up to `MWIN_HIT_REGIONS` (16) rects
    in logical units of the client area, each with a kind: client,
    caption, the eight edges and corners, minimize, maximize or close.
  - A later region lies over an earlier one, and the client is
    everywhere else.
  - The core keeps the regions at the call, and `mwinHitAt` is the one
    hit test every backend makes, so all agree on edges and overlaps.
  - Regions work on any window, including one the platform decorates.
- **Presses:**
  - A press on a caption or an edge moves or resizes the window through
    the platform. The program is told of neither the press nor its
    release.
  - Buttons stay the program's: it draws them and acts on their clicks.
  - A double click on a caption maximizes or restores the window, as
    every platform's own caption does.
- **Snap layouts:** `mwinSystemFacts.snapLayouts` is true on Windows 11
  (build 22000 and later, read with `RtlGetVersion`) and false
  elsewhere.
- **Win32:**
  - A custom chrome window keeps `WS_CAPTION` and its frame styles.
  - `WM_NCCALCSIZE` gives the client area the whole window. Maximized,
    it leaves out the frame Windows puts past the monitor.
  - One line of DWM frame is kept (`DwmExtendFrameIntoClientArea`), which
    keeps the shadow.
  - `WM_NCHITTEST` maps the regions to hit codes. Edges of a window that
    cannot be resized, or is maximized, are `HTBORDER`. A maximize button
    is `HTMAXBUTTON` when the window can maximize, which gives snap
    layouts.
  - The frame's mouse messages over the client rect come to the program
    as its own pointer records. A leave between the client area and a
    hit region is no leave. Presses on a maximize button are the
    program's and not Windows'.
  - The frame is sized again once the window's handle is known, since
    Windows sizes it before creation returns.
- **X11:**
  - Custom chrome asks for no decorations (`_MOTIF_WM_HINTS`).
  - A left press on a caption or an edge first gives up the X server's
    pointer grab, then sends `_NET_WM_MOVERESIZE` with the press's place
    on the desktop, the direction and button 1.
  - A second click on a caption sends `_NET_WM_STATE` for both
    maximized states.
- **Wayland:**
  - Custom chrome asks xdg-decoration for client-side decorations, and
    the backend's own frame is not drawn.
  - A left press on a caption is `xdg_toplevel.move`, and on an edge is
    `resize` from that edge.
  - A second click on a caption maximizes or restores the window.
  - A right press on a caption opens the window menu, as the backend's
    own frame does.
  - Popups have no toplevel and take no part.
- **The web** answers unsupported: a canvas has no frame to drag.

## Consequences

The contract tests cover:

- the refusals: too many regions, regions not finite, negative sizes
  and unknown kinds;
- the hit test: the last region wins, left and top edges are in, and
  right and bottom edges are out;
- a new window, even in a reused slot, starts with no regions.

Each backend is tested against the platform's own reading:

- **Win32:** reads the rects and hit codes, sends frame messages, and
  checks the maximized inset.
- **X11:** is driven through XTEST, with another client listening on the
  root window as a window manager would. The window is moved away from
  the root's corner so that a place on the desktop is not also a place
  in the window.
- **Wayland:** the test compositor records moves, resize edges, window
  menus, maximizes and the requested decoration mode.
