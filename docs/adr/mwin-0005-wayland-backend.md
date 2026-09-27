# mwin-0005. The Wayland backend

Status: Accepted

## Context

A Linux binary must run on a system with only X11 or only Wayland, so
the backends open their system libraries at run time (W7 in the design
notes). The protocol headers that `wayland-scanner` writes call
`libwayland-client` by name from inline functions, and the usual way
around that (GLFW, SDL) redirects those names to a process-wide table
of function pointers. The family allows no global mutable state
(`conventions.md`, section 6), and a static link with another library
that generated the same protocol code would define its interfaces
twice.

## Decision

- **Loading:** each context opens `libwayland-client.so.0` with
  `dlopen` when it starts, keeps the functions in its own table and
  closes the library when it stops. Nothing is process-wide. The
  headers' inline wrappers are never called: their libwayland names
  are poisoned, requests go through `mwinWlRequest` and the table, and
  a shared build links with `--no-undefined`, so a call by name fails
  the build.
- **Protocol code:** generated at build time from the XML of
  `wayland.xml`, xdg-shell, xdg-decoration, fractional-scale and
  viewporter. Every interface is renamed with an `mwin_` prefix, so
  the definitions never collide in a static link.
- **Selection:** the native backend is Wayland when `WAYLAND_DISPLAY`
  names a display; `wl_compositor` and `xdg_wm_base` are required,
  the rest are used when the compositor has them. Each global is bound
  at the lowest of the compositor's version and the version the
  backend implements.
- **Windows:** an `xdg_toplevel` on a `wl_surface`. The first
  configure makes the window. The compositor decides the mode, so a
  mode request is answered by the next configure: done when the mode
  was granted, denied otherwise. Minimizing is answered at once, since
  no configure reports it. A windowed toplevel sizes itself; its size
  request is denied in the other modes. Position, visibility, focus,
  aspect ratio, style changes and opacity have no request in these
  protocols and are answered unsupported.
- **Scale:** with the viewporter, the surface's logical size is its
  viewport's destination and the renderer's buffer is in pixels at any
  scale, fractional scaling included. Without it, the scale is the
  integer buffer scale.
- **Monitors:** each `wl_output` is a monitor from its first done
  event. Wayland names no primary output, so none is primary, and it
  reports no work area, which equals the bounds.
- **Keyboard:** the first seat's keyboard. `libxkbcommon` is opened
  like `libwayland-client`; without it the windows work and no keyboard
  is used. The keymap and modifier state come from the compositor.
  A key's code comes from its evdev code, and its meaning from the
  keymap's first level in the current layout group. Its text comes
  through the compose table of the locale (`LC_ALL`, `LC_CTYPE`,
  `LANG`), so dead keys compose; control characters are not text.
  Held keys repeat at the compositor's rate from the pump, at most
  eight per pump after a stall. Keys held when focus comes are not
  reported; focus that goes while keys are held posts a reset. A new
  keymap or layout group posts `mwin_eventKeyboardLayoutChanged`.
- **Pointer and touch:** the seat's pointer events are gathered into
  its frames, so a frame posts at most one motion, before its buttons,
  and one wheel record. A wheel turn counts the high-resolution steps
  (120ths of a detent) where the seat sends them, else the discrete
  steps, else the continuous distance at ten units to a detent; the
  kinds are never added together. Wayland has no double-click setting,
  so a press of the same button within 500 ms and 4 units of the last
  counts as one more click. Touch points are followed by id, sixteen
  at once, each on the window it began on; the seat's cancel cancels
  them all.
- **Cursors:** each window keeps the cursor mode and shape the program
  asked for, and the pointer shows them when it enters the window.
  Shapes use the cursor shape protocol, or else the cursor theme
  through `libwayland-cursor` (opened when present; `XCURSOR_THEME`,
  `XCURSOR_SIZE`), whose animated cursors show their first image. A
  captured cursor is a locked pointer, and raw deltas are the
  relative pointer's motion before acceleration, posted only while
  the window under the pointer holds it captured; a confined cursor is
  a confined pointer. Constraints are persistent, so the compositor
  applies them again whenever the pointer returns. A mode or shape the
  compositor's protocols cannot give is answered unsupported.
- **Input methods:** text-input-v3. A window that asks for text input
  is enabled, with its caret rectangle, whenever the seat's text input
  focuses on it. The input method's strings wait for its done event,
  which applies them in the protocol's order: the committed text, then
  the new composition as one underlined segment, its cursor as the
  caret and selection; a done without a composition ends it. The
  program's text is not shared with the input method, so it has none
  to delete around the caret. The on-screen keyboard has no request of
  its own in the protocol: `mwinRequestVirtualKeyboard` is answered
  unsupported, and a compositor shows its keyboard when text input is
  enabled.
- **Frame (W4):** a decorated window gets a frame of the backend's own
  where the compositor has no xdg-decoration or chooses client-side
  decorations. Five subsurfaces, committing on their own so the frame
  shows without the renderer: a 28-unit caption above the content,
  in the system's light or dark theme, with close, maximize (or
  restore) and minimize buttons drawn as anti-aliased shapes and no
  title text; and 6-unit invisible margins around it, whose corners
  reach 16 units along each edge. The caption moves the window, opens
  the window menu on a right click and maximizes on a double click;
  the margins resize it with the matching cursor. The window geometry
  takes in the caption, so the compositor's sizes and size limits are
  the frame's and the content is smaller by the caption. A maximized
  window keeps the caption alone, a full-screen one no frame. Pointer
  input on the frame never reaches the program. Its parts are drawn
  into shared memory from `memfd_create`, at the window's scale
  rounded up.
- **The pump** never waits: it flushes, reads what has arrived and
  dispatches it. A failed connection stops the loop, and `mwinRun`
  returns `mwin_errorPlatform`.

## Consequences

Building needs `wayland-scanner`, the `wayland-client` headers (1.22 or
later), `wayland-protocols` (1.32 or later) and the `xkbcommon` headers
(1.0 or later); running needs only `libwayland-client` 1.20 or later
and `libxkbcommon`, and only in a Wayland session. The integration
test runs against a headless weston and is skipped without
`WAYLAND_DISPLAY`; the input tests run against a small compositor of
their own on `libwayland-server` (`test/wayland_server.h`), since a
headless weston has no seat.
