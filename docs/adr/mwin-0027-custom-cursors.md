# mwin-0027. Cursors made from images

Status: Accepted

## Context

A program may draw its own pointer: a paint program's brush, a game's
crosshair, a resize grip the system has no shape for.
`mwinRequestCursorShape` picks one of the system's shapes; nothing
takes an image. GLFW, SDL3 and winit all make a cursor object once and
set it on windows, as cursors change on every hover and sending pixels
per change would copy them and remake the platform's objects each time.
SDL3 adds images of the same cursor for higher display scales. Each
platform takes a cursor's pixels in its own form: Win32 an icon with
`fIcon` false and a hotspot, X11 a RENDER picture, Wayland a surface
with a buffer scale, macOS an `NSImage` with a representation per
scale, the web a CSS `url()` that browsers ignore past 128 pixels a
side, Android a `PointerIcon` from a bitmap. iPadOS takes pointer
styles, not bitmaps.

## Decision

- **A cursor object.** `mwinCreateCursor` copies a `mwinCursorDef`'s
  images into the context's allocator and hands back a `mwinCursorId`;
  `mwinDestroyCursor` ends it. Ids are slot and generation, as windows'
  are, so a destroyed cursor's id is stale. Both run on the thread that
  runs the program, as requests do. Cursors still live when the context
  ends are given back with it.
- **The images.** One to `MWIN_CURSOR_IMAGES` (4), each in the icon's
  form (RGBA, straight alpha, 8 bits a channel, rows from the top, a
  stride), at most `MWIN_CURSOR_SIZE` (128) pixels a side, the web's
  limit, so a cursor that works on one platform works on all. The first
  is the cursor at scale 1; each one after is wider than the one
  before, for higher scales. A def that breaks any of this is refused
  at the call and counted as misuse.
- **The image a window takes.** The smallest image at least as wide as
  the first times the window's scale, else the largest. The hotspot is
  given in the first image's pixels and scaled to the image taken,
  rounded down and kept on it. Backends that size cursors in logical
  units (macOS, Wayland with a buffer scale) hand the platform all the
  images or the one it scales, as fits.
- **Showing it.** `mwinRequestCursorImage` asks for a cursor over a
  window, as `mwinRequestCursorShape` asks for a shape; each replaces
  the other. Destroying a cursor in use leaves its windows the default
  shape. The cursor mode still applies: a hidden or captured cursor
  stays hidden.
- **Where there is none.** iOS answers the request
  `mwin_outcomeUnsupported`, as it answers shapes; the cursor is still
  made, so portable code needs no branch.
- **Limits and backends.** The context's `cursors` limit (default 16)
  sizes the slots at creation. Backends make their platform objects
  lazily, per image, the first time a window shows it, and give them
  back through the optional `releaseCursor` operation before the images
  go.

## Consequences

A program converts its pixels once, into the form icons already take,
and switches cursors by id at the cost of a request. Images past 128
pixels, animated cursors and colour formats other than RGBA are not
taken; a program wanting them draws its pointer itself with the cursor
hidden. Each backend that serves images carries the code to make its
platform's cursor; until one does, it answers the request
`mwin_outcomeUnsupported`.
