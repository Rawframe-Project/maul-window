# mwin-0009. Gamepads

Status: Accepted

## Context

The requirements (section 7) give gamepads to Maul Window: a standard
location model named by where controls are, raw buttons, axes and hats
for unknown hardware, an id per connection with its vendor, product,
name and battery, hotplug records, no dead zones or other filtering,
and rumble where the latest request wins. W3 makes them an optional
component and W5 compiles the SDL_GameControllerDB mappings into
tables. This record sets the contract; each platform's gamepads follow
in their own changes.

## Decision

- **The component:** `MAUL_WINDOW_GAMEPAD`, on by default, builds the
  four functions of `maul-window/gamepad.h` and the backends' gamepad
  code; the header, the record types and the core's storage are always
  there, so a build without it fails to link a call, as muni-0013 has
  it.
- **Ids** are generation-checked, as windows' and monitors' (F17): one
  per connection, stale once the gamepad is disconnected, its slot
  free for another only after mwin_eventGamepadRemoved is drained. The
  `gamepads` limit (8 by default, 0 for none) bounds them.
- **Mapped or raw:** a gamepad whose layout the platform or the mapping
  data knows is mapped: fifteen buttons (the d-pad, four face buttons
  by compass point, shoulders, stick clicks, start, select, guide) and
  six axes (two sticks, two triggers). One it does not know is raw: up
  to 32 numbered buttons and 16 numbered axes, a hat as two axes. A
  record says which it is. Sticks run from -1 to 1, right and down
  positive, as the W3C standard mapping and most platforms have it;
  triggers from 0 to 1. Nothing is filtered.
- **Records:** hotplug and changes of a gamepad's facts are the
  context's notifications, naming no window, a change merging into one
  still waiting for the same gamepad. Buttons and axes are records in
  two rings of the context's, in the stream's one order, posted only
  when they change, whatever window has the focus. Buttons never
  merge: a button record that finds its ring full is lost, and the
  gamepad gets mwin_eventInputStateReset with no window and its id. An
  axis record that finds its ring full merges into the newest waiting
  record of that axis. The notifications limit must cover four records
  per gamepad (added, changed, reset, removed) as it covers three per
  monitor.
- **State:** mwinGetGamepadState reads the controls as the platform last
  reported them, which may be ahead of the records not yet drained; it
  is what a program reads after a reset, and what a program that polls
  reads each frame.
- **Rumble** is a plain call, not a request (F19 is for answers that
  come later): the backend runs the motors at once and returns, the
  latest call wins, a duration of 0 stops them. A gamepad says it has
  motors in its capabilities; one without answers
  `mwin_errorUnsupported`.

## Consequences

The test backend connects gamepads, presses their buttons, moves their
axes and records their rumble, and the contract tests run against it.
Linux (evdev), Win32 and the web follow, each with its own change; a
gamepad driver belongs to a platform rather than a window system, so
Wayland and X11 share Linux's.
