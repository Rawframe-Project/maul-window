# mwin-0006. The X11 backend

Status: Accepted

## Context

X11 remains the window system of many Linux desktops and of every
remote and virtual display (Xvfb, VNC, SSH forwarding), and the same
binary must run there as on Wayland.

## Decision

- **Loading:** each context opens `libxcb` with `dlopen` into its own
  table, and `libxcb-randr` where it is there, as the Wayland backend
  does its libraries (mwin-0005). XCB hands its replies over from the
  C library's `malloc`; they go back through the allocator module,
  the one place the library reaches the C library's memory.
  `libxkbcommon-x11` with `libxcb-xkb`, and `libxcb-cursor`, are
  opened the same way where they are there; without them the backend
  has no keyboard, or no cursor shapes.
- **Selection:** the native backend tries Wayland where
  `WAYLAND_DISPLAY` names a display, then X11 where `DISPLAY` names
  one. A backend that cannot reach its window system (its library
  missing, the connection refused) gives way to the next, with a
  fresh context.
- **Scale:** `Xft.dpi` from the root window's resources over 96, as
  desktops set it for their toolkits; 1 where it is not set. Sizes and
  places are logical, the pixels over the scale.
- **Windows:** made by the X server at once, so a creation reports
  them and completes in the same call; `mwin_eventShown` follows the
  map. The ICCCM and EWMH properties carry the title
  (`_NET_WM_NAME`), the close protocol and the ping, the size bounds
  and aspect ratio (`WM_NORMAL_HINTS`, where a window that does not
  resize is bounded to its size), decorations (`_MOTIF_WM_HINTS`),
  staying on top and the initial mode (`_NET_WM_STATE`) and opacity
  (`_NET_WM_WINDOW_OPACITY`). Size, place and visibility are asked of
  the X server, and its ConfigureNotify, MapNotify and UnmapNotify
  report them; a place comes from the window manager's synthetic
  event, or else from the X server.
- **Modes** belong to the window manager: a mode request asks it
  through `_NET_WM_STATE` (or ICCCM's `WM_CHANGE_STATE` to minimize)
  and is answered done when `_NET_WM_STATE` shows the mode, denied
  after half a second. Without an EWMH window manager
  (`_NET_SUPPORTING_WM_CHECK`), mode requests are answered
  unsupported. Focus goes through `_NET_ACTIVE_WINDOW`, or straight to
  the X server without a window manager.
- **Monitors:** RandR 1.5's monitor list, each monitor by the atom of
  its name, with the primary RandR names or else the first, and its
  change events; without RandR 1.5 the screen is the one monitor.
- **Keyboard:** the core keyboard through XKB. `libxkbcommon-x11`
  reads its keymap, XKB's state events keep modifiers and group
  current, and the keymap is read again when the keyboard or its map
  changes; the xkbcommon keyboard it shares with the Wayland backend
  gives meanings and text through compose sequences. With detectable
  autorepeat the X server's repeats are presses of a key already held,
  posted as repeats. There is no input method: XIM needs Xlib, so text
  comes from the keymap alone and a text input request is answered
  done with nothing to enable.
- **Pointer:** the core pointer, in the window's logical units; the
  X11 buttons 4 to 7 turn the wheel a detent each, 8 and 9 are back
  and forward. Crossings a grab makes are not reported. Quick clicks
  are counted by the backend, as on Wayland.
- **Cursors:** each window keeps its cursor, as X11 does: a shape
  from the cursor theme through `libxcb-cursor` (whose X11 names the
  core cursor font also has), or an empty cursor that hides the
  pointer. A confined cursor is a pointer grab confined to the window
  while it has focus. A captured cursor is a hidden confined one put
  in the window's middle, and its motion is XInput 2's raw motion
  (`libxcb-xinput`), the first two valuators before the X server's
  acceleration, posted as raw deltas while the window has focus;
  without XInput 2 capture is answered unsupported. The wheel stays on
  buttons 4 to 7: smooth scrolling through XInput 2's scroll valuators
  is left for later.
- **The pump** never waits: it flushes and handles every event that
  has arrived. A failed connection stops the loop.

## Consequences

Building needs the `xcb`, `xcb-randr`, `xcb-xkb`, `xcb-cursor`,
`xcb-xinput` and `xkbcommon-x11` headers; running needs only `libxcb`,
and the others for more than one monitor, the keyboard, cursor shapes
and capture. The
integration tests run against Xvfb, which has no window manager, the
input test driving it through XTEST; both are skipped without
`DISPLAY`.
