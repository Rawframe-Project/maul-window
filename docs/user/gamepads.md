# Gamepads

Gamepads belong to the context, not to a window: their records arrive whichever window has the focus. They are an optional component, `MAUL_WINDOW_GAMEPAD`, on by default; the `gamepads` limit (8 by default, 0 for none) bounds how many are connected at once.

## Connecting and reading

- `mwin_eventGamepadAdded` and `mwin_eventGamepadRemoved` report hotplug; each connection gets its own id, stale once the pad is gone. `mwin_eventGamepadChanged` reports a change of its facts, such as its battery. On Android a pad's capabilities can grow a moment after it is added, as Android merges its devices, so read them again on a change.
- `mwinGetGamepads` lists the connected pads and `mwinGetGamepadInfo` gives one's name, vendor and product, battery (percent, -1 where unknown or wired), whether it is mapped, and its capabilities.
- `mwin_eventGamepadButtonDown`, `mwin_eventGamepadButtonUp` and `mwin_eventGamepadAxisMoved` arrive only when a control changes. Button records are never merged: when their storage is full the pad gets `mwin_eventInputStateReset`. Axis records merge into the newest waiting one of their axis.
- `mwinGetGamepadState` reads every control as the platform last reported it, which may be ahead of the records not yet drained. A program that polls reads it each frame; a program that follows the records reads it after a reset.

Browsers show a page no gamepad until one of its buttons has been pressed there.

## Mapped and raw pads

A pad the platform or the SDL_GameControllerDB data knows is **mapped** onto a layout named by place:

- the d-pad, four face buttons by compass point (`mwin_padFaceSouth` is A on an Xbox pad and Cross on a PlayStation pad), the shoulders, the stick clicks, start, select and guide;
- two sticks from -1 to 1, right and down positive, and two triggers from 0 to 1.

Any other pad is **raw**: up to 32 numbered buttons and 16 numbered axes, each hat as two axes. Nothing is filtered, so dead zones and curves are the program's.

## Rumble and motion

- `mwinSetGamepadRumble` runs the grips' motors, the low-frequency (heavy) and high-frequency (light) one, for a duration; the latest call wins and a duration of 0 stops them. Pads with motors have `mwin_padRumble`; others answer `mwin_errorUnsupported`.
- `mwinSetGamepadTriggerRumble` runs the triggers' own motors on pads with `mwin_padTriggerRumble` (Xbox pads through Windows.Gaming.Input, and browsers whose actuator has it), apart from the grips'.
- On pads with `mwin_padMotion`, `mwinSetGamepadMotion` turns the sensors on (they start off, as they cost the pad's battery) and `mwinGetGamepadMotion` reads the latest acceleration in m/s² and rotation rate in rad/s, in SDL3's frame for a pad held in front of the player, with the angle turned about each axis since the last read. A program aiming with the gyroscope reads once a frame and loses no part of a quick turn. Motion comes from Linux's motion devices, GameController and Android 12 and later.

## Per platform

- **Linux:** evdev devices in `/dev/input`, for Wayland and X11 alike, found as they come and go. The user must be able to read them, as every desktop arranges for local users.
- **Windows:** Xbox pads through Windows.Gaming.Input, any number with their names and batteries, or XInput's four players where the runtime is missing. Other pads through Raw Input and the HID parser, mapped by the database. Windows.Gaming.Input may give a program in the background no input. A program that registers Raw Input for joysticks itself takes them from the library.
- **macOS and iOS:** GameController, which maps every pad it knows (macOS 11.3), with motors through CoreHaptics. iOS gives a background application no pad input.
- **Android:** the system's input devices, mapped by place where Android names a south face button, with rumble and batteries from Android 12.
- **The web:** the Gamepad API.
