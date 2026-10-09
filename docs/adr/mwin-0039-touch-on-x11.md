# mwin-0039. Touch on X11

Status: Accepted

## Context

The contract posts touch records (down, moved, up and cancelled, with
a stable id, the place and pressure where measured) on every backend
with a touch screen. The X11 backend asked for XInput 2.1 and selected
XI2 button and motion events only, so the server emulated the pointer
from a touch screen's first finger: a program got mouse records, with
no touch id and no second finger. XInput 2.2 adds touch events
(`XI_TouchBegin`, `XI_TouchUpdate`, `XI_TouchEnd`), selected on a
window the three together, which carry the touch's id and the valuators
the driver gives per touch; the server emulates no pointer for a client
that selects them. A device's touch class tells a touch screen (direct
touch) from a touchpad (dependent touch), whose fingers the drivers in
use send as pointer motion. A client without a grab gets a touch's
events only once it owns the touch, so it never sees one cancelled.

## Decision

- The backend asks for XInput 2.2. Smooth scrolling needs 2.1, touch
  2.2; a server with less answers less and gets no touch selection.
- Each window selects the three touch events with its button and
  motion events.
- Slave devices with a direct touch class are touch screens, read again
  with the other devices on a hierarchy change; their pressure valuator
  is the one labelled `Abs MT Pressure`, and one of no range is left
  out.
- A touch screen's begin, update and end post down, moved and up to the
  window the touch began in. The id is the device's number in the high
  32 bits and the server's touch number in the low. The place is in
  logical units. Pressure is the valuator over its range, clamped, kept
  per touch when an event leaves it out, -1 for a screen without one;
  16 touches are kept at once, and one past them carries the pressure
  of its own events.
- Touches of other devices make no record. No cancelled record is
  posted on X11.

## Consequences

Programs on X11 get every finger with its id, as on Wayland, Win32 and
the mobile backends, and no mouse record from a touch. The device
reading and the records are a module of their own (`x11_touch.c`),
tested with devices and events written out, since Xvfb has no touch
screen and XTest makes no touches; Xvfb does check that the version and
the selection are accepted.
