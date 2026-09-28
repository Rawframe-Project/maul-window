# mwin-0020. Smooth scrolling on X11

Status: Accepted

## Context

The X11 backend turned the wheel by buttons 4 to 7, a whole click each.
Touchpads and high-resolution wheels report finer movement only
through the scroll valuators of XInput 2.1. The X server gives an event
to a client in one form only, so reading valuators from XI2 motion
means reading all pointer events of a window from XI2. Window decision
W11 and its research note record the protocol.

## Decision

- **XI 2.1 or later:**
  - The backend announces XI 2.1. Each window selects the XI2 button and
    motion events of the master pointers.
  - Each such event is read as the core event it stands for, its place
    rounded down to the pixel, so the pointer keeps one code path. Enter,
    leave and keys stay core events.
- **Scroll valuators:**
  - They are read with `XIQueryDevice` from the slave pointers at the
    start, and again on `XI_HierarchyChanged` and on `XI_DeviceChanged`
    for a change of classes.
  - An XI2 motion's scroll valuators, keyed by source device and
    valuator number, make a fractional wheel event: the change over the
    increment, up for a falling vertical value, right for a rising
    horizontal one.
  - A motion that only scrolled reports no pointer motion.
  - The first value after the pointer enters, or after the master
    switches devices, only starts the count.
- **Emulated buttons:** wheel buttons flagged `XIPointerEmulated` are
  dropped. A wheel without scroll classes still turns by buttons 4
  to 7.
- **Chrome:** a caption press read through XI2 also releases its device's
  grab (`XIUngrabDevice`), besides the core one, before the window
  manager moves the window.
- **Without XI 2.1** (or libxcb-xinput): core events, as before.

## Consequences

Programs get smooth scrolling from touchpads and smooth wheels on X11,
in the same fractional units as on the other platforms.

- `x11_scroll` checks the arithmetic:
  - counting starts after a restart;
  - fractions and both directions;
  - negative increments;
  - other devices and other valuators;
  - the table's limit.
- All the X11 input tests now run through the XI2 path, since Xvfb has
  XI 2.2.
- Xvfb's devices have no scroll classes, so the reading of real
  devices' classes is not exercised by a test.
