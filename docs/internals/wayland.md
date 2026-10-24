# Wayland

## Protocol code without global state

The headers `wayland-scanner` writes call `libwayland-client` by name from inline functions. GLFW and SDL redirect those names to a process-wide table of function pointers; the family allows no global mutable state, and a static link with another library that generated the same protocol code would define its interfaces twice. So:

- the protocol code is generated at build time from the XML ([cmake/Wayland.cmake](../../cmake/Wayland.cmake)), every interface renamed with an `mwin_` prefix so definitions never collide;
- the headers' inline wrappers are never called: their libwayland names are poisoned, and requests go through `mwinWlRequest` and the context's table.

`protocols/` vendors the two protocols newer than the oldest `wayland-protocols` the build supports (xdg-toplevel-icon-v1, color-management-v1).

## Globals and versions

`wl_compositor` and `xdg_wm_base` are required; everything else is used when the compositor has it, and each global is bound at the lower of the compositor's version and the version the backend implements. A request whose protocol is missing answers unsupported.

## Windows and modes

A window is an `xdg_toplevel`; its first configure makes it. The compositor decides modes, so a mode request is answered by the next configure: done when granted, denied otherwise. Minimizing is answered at once, since no configure reports it, and a mode request it supersedes no longer waits for the next configure. Wayland gives no window a place, and has no request for visibility, aspect ratio or opacity.

With the viewporter, the surface's logical size is its viewport's destination and the renderer's buffer is in pixels at any scale, fractional scaling included; without it, the scale is the integer buffer scale.

Focus requests use xdg-activation: a token is asked for with the latest input serial and the focused surface, then the window is activated with it. The compositor says nothing back, so done means the activation was asked for. The first window shown activates itself with the launcher's `XDG_ACTIVATION_TOKEN`, which is then removed from the environment so the program's own children do not take it.

## The frame the backend draws

Where the compositor has no xdg-decoration, or prefers client-side decorations, a decorated window gets a frame of the backend's own ([wayland_frame.c](../../src/wayland_frame.c)): five subsurfaces committing on their own, so the frame shows without the renderer. A 28-unit caption in the system's theme with close, maximize and minimize drawn as anti-aliased shapes and no title text, and 6-unit invisible margins for resizing. The window geometry includes the caption, so the compositor's sizes are the frame's and the content is smaller by the caption. Pointer input on the frame never reaches the program. A popup draws no frame; a custom chrome window asks for client-side decorations and draws its own.

## Popups

A popup is an `xdg_popup` of its owner's surface with a positioner anchored at the top left of the owner's window geometry; the offset adds the caption. The protocol forbids an anchor rectangle outside the geometry, but an offset may leave it. The compositor may slide or flip the popup onto the output, and its configure says where, which is what the program is told. A menu grabs the seat with the latest input's serial; without one it shows without the keyboard, since the compositor would dismiss it at once.

## Input

- The first seat's pointer events are gathered into frames, so a frame posts at most one motion, before its buttons, and one wheel record. A wheel turn counts high-resolution steps (120ths of a detent), else discrete steps, else continuous distance at ten units to a detent, never adding the kinds together.
- Held keys repeat at the compositor's rate from the pump, at most eight per pump after a stall.
- A captured cursor is a locked pointer with the relative pointer's unaccelerated motion; a confined one a confined pointer. Constraints are persistent, so the compositor applies them again whenever the pointer returns.
- Pens come from the tablet protocol: once a client binds the tablet seat, the compositor stops emulating the pointer for its tools. Tablets are kept until removed, since a compositor tells a tool near a surface only to a client that kept its tablet.
- Cursor shapes use the cursor shape protocol, or else the cursor theme through `libwayland-cursor`.

## The pump

It never waits: it flushes, reads what has arrived and dispatches it. Selection and drop transfers run through non-blocking pipes a piece at each pump ([Clipboard and drops](clipboard-and-drops.md)).
