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

- **The mapping data:** SDL_GameControllerDB is kept as published in
  `tools/gamepaddb/` (F15), and `tools/gen_gamepad_db.py` compiles one
  platform's entries into `src/generated/`: each device by bus, vendor,
  product and version, and the distinct mappings they share, where each
  of the 21 controls comes from among the device's numbered buttons,
  axes, axis halves and hat directions, with the rare stick halves
  driven by buttons in a table apart. Entries for SDL's own drivers are
  left out, and a later entry for a device replaces an earlier one, as
  in SDL. A device matches its exact version, else another version of
  itself. The Linux tables hold 731 devices and 268 mappings in about
  19 KB.
- **Linux:** gamepads are evdev devices, for the Wayland and X11
  backends alike. A context finds those in `/dev/input` when it starts
  and watches the directory with inotify for more, retrying a node when
  its permissions change; a device that is gone reads as gone. A
  gamepad or joystick is a device with joystick or gamepad buttons and
  none of a keyboard's, mouse's, tablet's or motion sensor's. Its
  buttons, axes and hats are numbered as SDL numbers them (buttons from
  `BTN_JOYSTICK` up, then those below; axes in code order, the hats
  apart), so the database's mappings apply; one the database lacks is
  mapped by the kernel's gamepad layout when it has `BTN_SOUTH`, and is
  raw otherwise. Devices are read without blocking at each pump, their
  events stamped on the monotonic clock (`EVIOCSCLOCKID`), a report's
  worth at a time; dropped events are answered by reading the whole
  state again. Rumble is `FF_RUMBLE` force feedback, where the device
  has it and its node opens for writing.
- **Win32:** gamepads are XInput's four players, through the newest
  XInput DLL the system has, loaded when the context starts; without
  one there are no gamepads. A connected player is read at each pump
  and its controls posted only when XInput's packet number moves; the
  free players are asked about every half second, since asking about
  an empty one is slow. XInput pads are mapped by their fixed layout,
  with no guide button (XInput does not report it) and no vendor or
  product; a wireless pad's battery comes in XInput's four levels.
  Rumble drives the heavy motor from the low frequency and the light
  one from the high, and the backend stops it when its duration runs
  out, as XInput keeps a motor running until told otherwise. Other HID
  gamepads (through Raw Input and the database's Windows entries) are
  later work.
- **Web:** gamepads are the Gamepad API's, `navigator.getGamepads()`
  read at each pump for the first eight indexes, a pad's controls
  posted when its timestamp moves. A pad with the standard mapping is
  mapped (its analog triggers are buttons 6 and 7 there, their values
  the trigger axes), any other raw. A pad's index can be reused, so the
  page's disconnections are counted per index and a pad found there
  after one is a new pad even when no pump saw the index empty. The
  name, vendor and product come from the id, as Chrome ("Name (...
  Vendor: 045e Product: 028e)") and Firefox and Safari ("45e-28e-Name")
  write it. Rumble is the vibration actuator's `dual-rumble` effect,
  which the browser stops when its duration ends; a duration of 0
  resets the actuator. The page gives no battery. Browsers show a page
  no gamepad before one of them is pressed there.

## Consequences

The test backend connects gamepads, presses their buttons, moves their
axes and records their rumble, and the contract tests run against it.
A gamepad driver belongs to a platform rather than a window system, so
Wayland and X11 share Linux's, tested with virtual devices made through
uinput (CI lets its test user make them). Win32's driver is tested
against a stand-in for XInput, as no virtual XInput device can be made;
the web's in headless Chrome against a stand-in for
`navigator.getGamepads()`, for the same reason.
