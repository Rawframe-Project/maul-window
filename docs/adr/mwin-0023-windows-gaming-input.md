# mwin-0023. Xbox gamepads through Windows.Gaming.Input

Status: Accepted

## Context

The Win32 backend reads Xbox-compatible pads through XInput
(mwin-0009). XInput numbers four players and no more, names no pad and
gives no USB ids, and reaches only the two motors in the grips. The
requirements name Windows.Gaming.Input or GameInput beside XInput.
Windows.Gaming.Input is part of every Windows since 10; GameInput needs
a redistributable that not every system has. Both give the same pads
XInput does, so a pad read through one must not be read through
another too.

## Decision

- **Which API:**
  - When a context starts, the backend looks for Windows.Gaming.Input:
    `RoGetActivationFactory` and the string functions from
    `combase.dll`, loaded at run time, and the `Gamepad` class's
    statics. It initializes COM on the thread for that, as the drop
    target does.
  - Where the runtime is found, it reads the Xbox pads and XInput is
    not loaded. Where it is not (Windows before 10, a system without
    the class), XInput reads them as before.
  - Generic HID pads stay with Raw Input (mwin-0021), which leaves
    XInput's devices out, and so the runtime's.
- **Declarations:** the interfaces are declared in the source, with
  their ids from the runtime's metadata, rather than taken from the
  SDK's headers. Those headers' C declarations differ between SDK
  versions and MinGW's, and a declaration here names only the methods
  it calls.
- **Finding pads:**
  - The pads are listed when the runtime says one came or went, and
    every half second too, since the list fills after the class starts
    and a notification can be missed.
  - The runtime calls its handler on a thread of its own. The handler
    only sets a flag, which the pump takes; it is agile and keeps its
    own references, since the runtime may hold it past the context.
  - A pad is the same object each time it is listed. Up to sixteen are
    tracked, and the `gamepads` limit bounds them as it does any.
- **Reading:** each pad's reading is taken at each pump and posted when
  its time moves. Its buttons map by place as XInput's do, Menu as
  start and View as select, with no guide button, which the runtime
  does not give. Sticks' y is turned so down is positive. The paddles
  of Elite pads have no place in the mapped layout and are not posted.
- **Facts:** the name, vendor and product come from the pad's raw
  controller; a pad without them is "Xbox controller". The battery is
  the report's remaining capacity over its full capacity, read when the
  pads are looked for, and a change is posted as mwin_eventGamepadChanged.
  A pad with no battery, or one that reports none, has -1.
- **Rumble** sets the grips' motors. The runtime keeps them running
  until told otherwise, so the backend stops them when the duration
  runs out, as with XInput.
- **Tests:** the runtime is a table of functions, and the tracking runs
  against a stand-in for it. The real runtime is started, listed and
  stopped where the system has it.

## Consequences

Programs see any number of Xbox pads on Windows 10 and later, with
their names and USB ids, and batteries that change.

The runtime is made for the program in the foreground, and may give a
process in the background no input where XInput kept reading: a
program whose windows are all in the background should not count on
its Xbox pads. None of the machines the tests run on has a pad to show
which.

- `pad_tracker` runs against the stand-in, on every platform (the
  tracking moved there when the macOS backend came to share it,
  mwin-0024):
  - a pad found at once when the runtime says so, and otherwise not
    sooner than every half second;
  - six pads, more than XInput's four;
  - the facts, a battery's change, and the buttons, sticks and
    triggers, posted only when the reading's time moves;
  - rumble stopped when its time runs out;
  - a removal, and every reference the stand-in gave let go.
  - values past their ranges kept in them.
- `win32_wgi` runs the real runtime on Windows, and under wine, which
  has the class, with no pad attached.

Amended 2026-09-30: the tracking (looking for pads, reading them when
their time moves, batteries, stopping rumbles) is now the pad tracker,
which the macOS backend's GameController pads share. The runtime's
table gives readings in the contract's terms, so View as select, Menu
as start and the sticks' turned y are this backend's mapping.
