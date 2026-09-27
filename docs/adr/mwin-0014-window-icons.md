# mwin-0014. Window icons

Status: Accepted

## Context

The requirements (section 4) list a window's icon among its
properties, as a bounded image. Every desktop shows a window's icon in
more than one size (a title bar, a taskbar, a window switcher) and
picks from the sizes a program gives, or scales one; Wayland had no way
to set one until xdg-toplevel-icon-v1 (wayland-protocols 1.37, newer
than the distributions supported ship); a page has one icon for all its
canvases.

## Decision

- **A request of a window:** `mwinRequestIcon` with up to
  `MWIN_ICON_IMAGES` (4) images of up to `MWIN_ICON_SIZE` (256) pixels
  on a side, RGBA with straight alpha and a stride, copied at the call
  without the rows' padding into one block the request holds. No
  images asks for the platform's own icon. Nothing about it joins the
  window's state: no platform reports it back.
- **The image for a size** is the smallest at least as large, else the
  largest, one rule for every backend that picks.
- **Win32:** a big and a small icon (`WM_SETICON`) from the images for
  `SM_CXICON` and `SM_CXSMICON` at the window's DPI, each a 32-bit DIB
  with straight alpha and an empty mask. The icons the backend made are
  destroyed when replaced and after the window.
- **X11:** `_NET_WM_ICON` with every image, its width, height and ARGB
  pixels, for the window manager to pick; none deletes it. An icon past
  the server's largest request is too large.
- **Wayland:** xdg-toplevel-icon-v1, vendored (`protocols/`): each
  image in a square shared-memory buffer of its own, as the protocol
  requires, centred on a clear square when it is not one, its alpha
  premultiplied as Wayland's buffers have it, at scale 1. The icon is
  set, the surface committed so it applies, then the icon object and,
  after it, the buffers destroyed, which the protocol allows. Without
  the protocol, unsupported.
- **The web** answers unsupported.

## Consequences

The test backend writes down the images it was given as a count and a
checksum (`mwinTestGetIcon`). Each backend is tested against the
platform's own reading of the icon: Win32 reads its icons back with
`WM_GETICON` and `GetDIBits`, X11 reads the property from another
client, and the Wayland test compositor records the buffers as they
are when set and whether one went before its icon.
