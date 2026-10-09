# mwin-0037. Pens on Wayland and X11

Status: Accepted

## Context

The contract makes a pen a typed optional capability that posts pen
records (position, pressure, tilt, eraser, barrel button, hover) and no
mouse record. Win32, macOS, iOS, Android and the web posted them; the
Linux backends posted none, and a tablet's pen moved the mouse there.
Wayland has the tablet protocol (`zwp_tablet_manager_v2`), whose tools
carry pressure, tilt, buttons and proximity; a compositor stops
emulating the pointer for a tool once the client binds the seat's
tablet seat. X11 has no tablet protocol: a tablet's driver makes slave
pointer devices whose XInput2 events carry pressure and tilt as
valuators, and the device's X input type (`STYLUS`, `ERASER`), its name
or a pressure valuator tells a pen from a mouse. Xvfb has no tablet.

## Decision

- **Wayland:** the first seat's tablet seat from
  `zwp_tablet_manager_v2`. A tool's changes gather until `frame`, then
  post: button records, then down, up or moved. Pen, brush, pencil and
  airbrush tools are pens; the eraser sets the eraser flag; mouse, lens
  and finger tools post pen records without tilt, as a client bound to
  the tablet seat gets no emulated pointer for them. Tablets are kept
  until removed, since a compositor tells a tool near a surface only to
  a client that keeps the tool's tablet; pads are let go of.
- **Values:** pressure is `pressure / 65535` on Wayland and the
  valuator over its range on X11; tilt is in degrees, clamped to ±90;
  `BTN_STYLUS` (X11's button 2) is the barrel; the second barrel button
  makes no record.
- **Cursor:** on `proximity_in`, the window's cursor through the cursor
  shape protocol's tablet tool device, or the theme's surface. A cursor
  made from images shows over the pointer only.
- **X11:** slave pointer devices of X input type `STYLUS` or `ERASER`
  are pens; one of type `TABLET` or none is a pen by an eraser in its
  name, then by an `Abs Pressure` valuator; a device of any other type
  is no pen, since touch screens carry a pressure valuator too. Their
  XI2 motion, press and release become pen records and no mouse
  record: contact from button 1, pressure and tilt from the labelled
  valuators, each kept from its last value, as an event carries only
  the changed ones. The devices are classified again on
  `XI_HierarchyChanged`.
- **Tests:** the Wayland path against the test compositor's tablet; on
  X11 the classification, scaling and routing as whitebox tests over
  device descriptions and XI2 event bytes.

## Consequences

A pen on Linux behaves as on every other backend: pen records and no
mouse record. Real X11 tablet drivers are not run in CI, since Xorg with
uinput devices would bring root and kernel modules into it; the
whitebox tests stand for them, and a driver whose devices break the
classification is a bug to fix with its description added to them.
