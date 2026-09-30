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
- **Native handles** give the view and its layer, for the GPU layer to
  make its surface from.
- **Slices:** the first slice makes windows, monitors and the loop.
  Input, gamepads, the clipboard, dialogs and the rest follow, and are
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
