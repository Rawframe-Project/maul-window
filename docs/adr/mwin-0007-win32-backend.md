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
- **Keys** are known by scan code, extended keys apart, so a code
  names the same key on every layout; a key sent without one gets it
  from its virtual key. Its meaning is what `ToUnicodeEx` makes of it
  with no modifier, leaving a waiting dead key alone. Text comes from
  `WM_CHAR`, a character outside the BMP in two messages. Print
  Screen, which Windows sends only released, is pressed and released
  together. F10 and Alt alone would enter the menu mode of a window
  with no menu, and Alt with a key would beep; all three are kept from
  Windows, while Alt+F4 and Alt+Space stay its own. The layout's name
  is its language's BCP 47 tag.
- **The mouse:** a window asks for `WM_MOUSELEAVE` when the pointer
  enters and keeps the mouse while a button is held. Quick clicks
  follow Windows' double-click time and distance rather than the
  class's `CS_DBLCLKS`, so every button counts them alike.
- **Cursors** are set in `WM_SETCURSOR` over the client area, from the
  system's shapes or none. A confined cursor is clipped to the client
  area while the window has focus, again after it moves or resizes; a
  captured one is hidden and clipped to a point in the middle, and the
  mouse's raw input, registered on the first capture, brings its
  motion before acceleration.
- **Touch and pen** come from the `WM_POINTER` messages; the mouse
  keeps its own, as the pointer messages would change what every
  mouse program sees. The touch and pen messages never reach
  `DefWindowProc`, so Windows makes no mouse input of them. Up to ten
  touches are followed on a window by their pointer ids; one that
  Windows takes while down, by a cancelled lift or a lost capture, is
  cancelled. A pen's pressure and tilt come when the pen measures
  them; without a pressure it presses fully or not at all. The eraser
  end and the eraser button both count as the eraser.

## Consequences

The backend builds with clang-cl in CI and with clang for mingw, and
its tests run on the Windows runner's desktop and under wine, input
driven through `SendInput`, touch injection and a synthetic pen; wine
injects neither, so touch and pen are tested on the runner only.
Input methods, system facts and the dark title bar follow in their own
changes.
