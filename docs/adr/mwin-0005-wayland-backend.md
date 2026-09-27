# mwin-0005. The Wayland backend

Status: Accepted

## Context

A Linux binary must run on a system with only X11 or only Wayland, so
the backends open their system libraries at run time (W7 in the design
notes). The protocol headers that `wayland-scanner` writes call
`libwayland-client` by name from inline functions, and the usual way
around that (GLFW, SDL) redirects those names to a process-wide table
of function pointers. The family allows no global mutable state
(`conventions.md`, section 6), and a static link with another library that
generated the same protocol code would define its interfaces twice.

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
- **The pump** never waits: it flushes, reads what has arrived and
  dispatches it. A failed connection stops the loop, and `mwinRun`
  returns `mwin_errorPlatform`.

## Consequences

Building needs `wayland-scanner`, the `wayland-client` headers (1.22 or
later), `wayland-protocols` (1.32 or later) and the `xkbcommon` headers
(1.0 or later); running needs only `libwayland-client` 1.20 or later
and `libxkbcommon`, and only in a Wayland session. The
integration test runs against a headless weston and is skipped without
`WAYLAND_DISPLAY`; the input tests run against a small compositor of
their own on `libwayland-server` (`test/wayland_server.h`), since a
headless weston has no seat. The pointer, touch, text input and a
client-side frame for compositors without server-side decorations
follow in their own changes.
