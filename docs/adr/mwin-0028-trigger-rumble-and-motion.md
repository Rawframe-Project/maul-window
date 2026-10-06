# mwin-0028. Trigger rumble and motion

Status: Accepted

## Context

Xbox One and later pads have a motor in each trigger beside the two in
the grips; Sony's and Nintendo's pads have an accelerometer and a
gyroscope, which games use for aiming and tilt. Windows.Gaming.Input
takes all four motors in one value; SDL3 and the web run the triggers
by a call or an effect of their own. Motion sensors run at 250 to 1000
samples a second, cost the pad's battery and its link, and some
platforms want them turned on (GameController's
`sensorsRequireManualActivation`). Gyro aiming integrates every
sample; reading the latest rate once a frame loses most of a quick
turn. Linux's evdev has no trigger effect; its motion comes from a
second device the driver makes. Windows' HID and XInput paths have
neither.

## Decision

- **Capabilities.** `mwin_padTriggerRumble` and `mwin_padMotion` join
  `mwin_padRumble`; a backend grants each per pad where the platform
  serves it, and the calls answer `mwin_errorUnsupported` without it.
- **Trigger rumble.** `mwinSetGamepadTriggerRumble` runs the triggers'
  motors, 0 to 1 each, for a time or until the next call, as
  `mwinSetGamepadRumble` runs the grips': the latest call wins, a
  duration of 0 stops them, and neither call touches the other's
  motors. Where the platform takes the four motors as one value or one
  effect, the backend combines them and stills a pair when its time
  runs out first.
- **Motion.** Off when a pad connects; `mwinSetGamepadMotion` turns it
  on or off, and either way starts it anew. `mwinGetGamepadMotion`
  reads an `mwinGamepadMotion`: the latest acceleration in m/s^2 with
  gravity and the latest rotation rate in rad/s, for a pad held in
  front of the player (x to the right, y up, z toward the player,
  counter-clockwise positive, SDL3's frame), the latest sample's time,
  and the angle turned about each axis since the last read, every
  sample's rate over its time summed by the core. A sample more than
  100 ms after the one before counts 100 ms, so a pause in the stream
  turns nothing far. No records: a 1000 Hz stream stays out of the
  bounded queue.
- **Where.** Trigger rumble on Windows.Gaming.Input for Microsoft's
  pads from the Xbox One on, on the web where the actuator has
  `trigger-rumble`, and later through GameController's trigger
  haptics. Motion on Linux's motion devices, GameController and
  Android 12 and later, each in its own change. Windows motion would
  need each pad's HID reports parsed and waits for a measured need.

## Consequences

A program asks for trigger rumble as it asks for rumble, and for
motion with one call to turn it on and one read a frame; a pad without
either says so in its capabilities. Motion's units and frame are the
same on every platform, the backends converting. Effects beyond a
strength and a duration (adaptive triggers, haptic waveforms) are not
taken.
