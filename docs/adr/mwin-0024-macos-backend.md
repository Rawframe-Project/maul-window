# mwin-0024. The macOS backend

Status: Accepted

## Context

The requirements name macOS among the platforms, with AppKit windows
whose views hold a CAMetalLayer that Maul RHI's Metal driver presents
to. AppKit owns the application's loop: `[NSApp run]` does not return
to its caller between events, and windows, screens and input arrive as
messages to objects on the main thread. The contract's `mwinRun` keeps
the loop for platforms that own it (the web's, mwin-0022), through
`mwinStartProgram`, `mwinStepProgram` and `mwinEndProgram`.

## Decision

- **Language:** the backend is Objective-C, compiled with manual
  retain and release (`-fno-objc-arc`) at the C23 level the rest of the
  library uses, as Maul RHI's Metal driver is. Only the backend's files
  are Objective-C; the core stays C. It links AppKit and QuartzCore.
- **The loop:**
  - `mwinRun` starts the program, then runs the application. Its frames
    come from a display link of the main screen on macOS 14 and later,
    and from a timer at 60 Hz before it or without a screen, both in the
    run loop's common modes so that a live resize keeps frames coming.
  - The frame that stops the program stops the application, which
    returns from `[NSApp run]`; `mwinRun` ends the program and returns.
    An application-defined event wakes the loop so that the stop takes
    effect at once.
  - Events AppKit delivers between frames are posted when they come,
    and the next frame reads them.
  - Code of the program that spins the run loop (a modal panel, an
    input method waiting on its server) would have the display link
    call a frame inside a frame; a tick that comes while init, a frame
    or quit runs is skipped.
- **Windows:**
  - A window is an `NSWindow` whose content view is layer-backed with
    a `CAMetalLayer` of its own. Its delegate posts closing, size,
    place, screen, scale, focus, minimizing, full screen and occlusion.
    The close button asks rather than closes.
  - Sizes are in points and pixel sizes in the backing's pixels. Places
    are measured from the primary screen's top left, down, as on every
    other platform, so AppKit's bottom-up coordinates are turned.
  - Full screen is AppKit's own, in a space of its own; the request
    completes when AppKit says it entered or left it, or failed.
- **Monitors** are the screens, keyed by their display number, with
  names, bounds, work areas, scales and top refresh rates. A change of
  the screens' parameters reads them again, and posts what came, went
  or changed.
- **Keys:**
  - A key is known by its virtual key code, which names the same
    physical key on every layout and maps to the contract's codes. Help,
    where other keyboards have Insert, is Insert; the keypad's Clear,
    where Num Lock is, is Num Lock. On ISO keyboards macOS gives the key
    left of 1 and the key right of the left Shift each other's codes,
    which the backend turns back. Fn and the volume keys have no code.
  - A key's meaning is what the current keyboard layout gives it with no
    modifier (`UCKeyTranslate` on the input source's layout data, a dead
    key giving its accent), read again when the input source changes,
    which also posts `mwin_eventKeyboardLayoutChanged`. Input methods
    with no layout data use the current ASCII-capable layout.
  - Modifier keys come as `flagsChanged:`, and each key's own bit in the
    flags tells a press from a release. macOS tells only that Caps Lock
    toggled, so each toggle posts a press and a release. macOS has no
    Num Lock state.
  - AppKit keeps a key's release from the window while Command is held;
    an event monitor of the application hands those releases on.
  - The layout's name is its localized name ("U.S.", "ABC").
- **Text** comes through the view's text input client, as AppKit's key
  bindings give it, without control characters. A key pressed with
  Command types nothing: it is a shortcut.
- **Input methods:**
  - A window that does not accept text gives its view no input
    context, so the key bindings still type text but no input method
    composes, as a Win32 window without an input context.
  - Marked text comes as `mwin_eventImePreedit`. Its clauses are the
    ranges of the marked clause attribute, neighbours of one style
    joined; the clause with a thick underline is the target, the rest
    are to convert. The target's span is the selection; without one,
    the method's selected range is. The caret is at the end of the
    method's selected range.
  - Committed text comes as text, then the composition ends. Marked
    text AppKit accepts as it is (`unmarkText`) is committed the same
    way. Turning text input off discards a composition without
    committing it.
  - The candidate window goes by the caret the program gave, whatever
    range the method asks about: the view keeps no text of its own.
- **Cursors:**
  - A window's cursor is its view's cursor rectangle: the shape's system
    cursor, or a blank one while the mode hides it. The Move shape is
    the open hand, macOS's cursor for what can be moved; the diagonal
    resizes are macOS 15's frame resize cursors, and the arrow before
    it; waiting and progress are the arrow, since macOS shows its own
    busy cursor over a program that does not answer.
  - A captured cursor is moved to the middle of the view, parted from
    the mouse and hidden while the window is the key window; the
    mouse's motion comes as `mwin_eventRawPointerDelta` from the
    events' deltas, which macOS has already accelerated. It comes back
    when the window loses focus or is destroyed.
  - macOS has no way to keep a cursor inside a window other than
    warping it back after it has left, which shows and loses motion, so
    confinement is unsupported.
- **Mouse:** buttons, motion and drags from the view's mouse methods,
  and entering and leaving from a tracking area over the whole view,
  active whether or not the window has focus. A click that activates a
  window is the program's too. Click counts are AppKit's, which follow
  the user's double-click setting. Buttons beyond the third are Back
  and Forward.
- **Pen:**
  - A tablet's pen drives mouse events with the tablet point subtype.
    Those become pen records and no mouse record, as on the other
    platforms: the tip's press and release are the pen's down and up, a
    drag moves it touching, a move hovering. Tablet point events of
    their own move it as well.
  - Pressure is the event's, 0 while hovering. Tilt, which AppKit gives
    from -1 to 1 along each axis, is scaled to 90 degrees, with AppKit's
    upward y turned down.
  - The barrel is the lower side button of the event's button mask; its
    press and release are pen button records. The upper side button,
    which tablet drivers give to their own commands, is left out.
  - Proximity events tell which end is near the tablet: the eraser's
    sets `mwin_penEraser` until the pen leaves.
- **Gamepads** come through GameController:
  - Every controller with an extended gamepad profile, which
    GameController maps by place, so each is mapped: its face buttons
    by place, Menu as start, Options as select, Home as guide, the
    sticks' y turned so down is positive.
  - Their tracking is Windows.Gaming.Input's (mwin-0023), shared as the
    pad tracker: pads looked for when GameController says one came or
    went, and every half second; read at each pump, posted only when
    the profile's last event time moves; batteries read when looked
    for.
  - The name is the vendor name; GameController gives no USB ids, so
    they are 0. The battery is its reported charge, -1 while its state
    is unknown, as a wired pad's is.
  - Pads are read whether or not the program is in front
    (`shouldMonitorBackgroundEvents`), as on the other desktop
    platforms. They need GameController of macOS 11.3; before it no
    pad is found.
  - The motors are CoreHaptics engines, one per grip where the pad's
    haptics reach each grip (the left the heavy, low motor, the right
    the light, high one), otherwise one for the whole pad running at the
    stronger of the two; a pad with no haptics has no
    `mwin_padRumble`. Each engine plays one endless continuous event
    whose intensity follows its motor, and stops it at 0; the tracker
    stops a rumble when its time runs out. Engines are made on a pad's
    first rumble and kept until the pad goes or the pads stop.
    CoreHaptics tells of an engine's reset or stop on a queue of its
    own, so no handler is set: a call that fails drops the engine, and
    the motor is made again once.
- **Wheel:** precise scrolling (touchpads, Magic Mouse) comes in points,
  ten to a detent, as the Wayland backend's continuous scrolling does; a
  wheel's comes in lines, one to a detent. Its sign is what the user's
  scrolling direction makes it, as on the other platforms: natural
  scrolling is not undone.
- **Native handles** give the view and its layer, for the GPU layer to
  make its surface from.
- **Slices:** the first slice makes windows, monitors and the loop; the
  second keys, text, the mouse and the wheel; the third input methods,
  cursors and capture; the fourth the pen; the fifth gamepads and their
  motors. The clipboard, dialogs and the rest follow, and
  are unsupported until then.

## Consequences

Programs open windows on macOS with Metal layers, and their frames
follow the screen's refresh on macOS 14 and later.

Frames keep coming during a live resize and while a menu is open, since
both sources run in the common modes. A frame that takes long holds up
AppKit's events as well, since both run on the main thread.

- `macos` runs against the CI runner's session: what a creation
  reports, the monitors, the view and its layer, a title, a size and a
  place as AppKit reports them, maximizing and back, hiding, showing
  and destroying.
- `macos_input` makes the NSEvents AppKit would deliver and hands them
  to the window: a key's press and release with the layout's meaning,
  its text, the left Shift's press and release, a press, a drag and a
  release at their places with click counts, and a wheel's lines;
  Command and a key through the application's queue where the window
  is the key window, which a CI runner's session never makes it. Real
  input cannot be made on the runner without permissions.
  It also asks for a shape and each cursor mode: confinement is
  unsupported and the rest are done.
- `macos_pen` makes tablet events through Quartz, the only way to give
  them the tablet subtype, and hands them to the view: hovering, the
  tip's down with pressure and tilt, a drag, the barrel's press and
  release, lifting, and the eraser end; no mouse record comes of them.
  Such events have no window, so their places are not checked.
- `macos_gamepad` runs a program with GameController watched for half
  a second and lists its pads, each mapped and named. No pad is
  attached to the runner; the tracking, rumble's timing included, runs
  against a stand-in in `pad_tracker` on every platform. The motors
  themselves have run on no machine the tests run on.
- `macos_ime` calls the view's text input client as an input method
  would: no input context before text input is on and one after, the
  caret the candidate window goes by, two clauses with the target and
  its selection in bytes, a commit and the composition's end, and a
  composition that turning text input off ends without committing.
