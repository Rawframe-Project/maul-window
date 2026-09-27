# mwin-0007. The Win32 backend

Status: Accepted

## Context

Windows is the first desktop of most players and developers. Its
windows belong to the thread that made them, its sizes are in pixels
of monitors that each have their own DPI, and moving or sizing a window
runs a loop of Windows' own inside the message dispatch.

## Decision

- **Linking:** the backend links `user32` and `shcore`, which every
  Windows has; nothing is loaded at run time. It needs Windows 10
  version 1703 or later, for per-monitor DPI awareness version 2.
- **DPI:** a context makes the process per-monitor DPI aware (version
  2) when it starts, unless the process chose an awareness itself, in
  its manifest or by a call; it never changes one the process chose.
  Each window has its monitor's DPI; sizes and places are logical, the
  pixels over the DPI's scale. A window that moves to a monitor of
  another DPI takes the size Windows suggests, which keeps its logical
  size, and posts the scale change.
- **Windows:** one window class, `MaulWindow`, registered when a
  context starts and unregistered when it stops. A window is made at
  its logical size for the DPI of the monitor it opens on. The window
  procedure's messages post the records: sizes and modes from
  `WM_SIZE`, places from `WM_MOVE`, focus, visibility, the close
  button's `WM_CLOSE`, and new monitors from `WM_DISPLAYCHANGE`. Size
  limits bound the frame in `WM_GETMINMAXINFO`, and the aspect ratio
  holds as the user drags an edge in `WM_SIZING`.
- **Modes:** maximizing and minimizing are Windows' own. Borderless
  full screen is a popup window covering its monitor; leaving it gives
  the window back its style and placement. Mode requests are answered
  at once, their records following from the messages.
- **Requests:** a focus request is denied when Windows keeps the
  foreground from the process. Opacity makes the window layered.
  Staying on top is the topmost band.
- **The size-move loop:** while the user moves or sizes a window,
  Windows' own loop runs inside the dispatch; a timer in it runs the
  program's frames, so rendering goes on.
- **The pump** never waits: it dispatches every message the thread
  has.

## Consequences

The backend builds with clang-cl in CI and with clang for mingw, and
its test runs on the Windows runner's desktop and under wine. Input
(keyboard by scan code, text, the mouse, raw input, touch and pen,
cursors, input methods), system facts and the dark title bar follow
in their own changes.
