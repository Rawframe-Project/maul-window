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
  Command types nothing: it is a shortcut. Compositions follow with the
  input methods.
- **Mouse:** buttons, motion and drags from the view's mouse methods,
  and entering and leaving from a tracking area over the whole view,
  active whether or not the window has focus. A click that activates a
  window is the program's too. Click counts are AppKit's, which follow
  the user's double-click setting. Buttons beyond the third are Back
  and Forward.
- **Wheel:** precise scrolling (touchpads, Magic Mouse) comes in points,
  ten to a detent, as the Wayland backend's continuous scrolling does; a
  wheel's comes in lines, one to a detent. Its sign is what the user's
  scrolling direction makes it, as on the other platforms: natural
  scrolling is not undone.
- **Native handles** give the view and its layer, for the GPU layer to
  make its surface from.
- **Slices:** the first slice makes windows, monitors and the loop; the
  second keys, text, the mouse and the wheel. Compositions, cursors,
  capture, the pen, gamepads, the clipboard, dialogs and the rest follow, and are
  unsupported until then.

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
