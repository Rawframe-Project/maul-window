# mwin-0025. The iOS backend

Status: Accepted

## Context

The requirements name iOS among the platforms, with UIKit views whose
CAMetalLayer Maul RHI's Metal driver presents to. UIKit owns the main
thread and its loop: `UIApplicationMain` never returns, and the
application learns of its windows, screens, input and life through
delegates and messages on the main thread. Applications built with
current SDKs must take the scene life cycle, or stop at launch
("UIScene life cycle is required for apps built with this SDK", iOS 27
SDK). The contract allows an `mwinRun` that does not return (the web's,
mwin-0022).

## Decision

- **Oldest iOS:** 15.0, what every API the backend uses needs, and the
  deployment target of the simulator's toolchain file and of a build of
  the library alone for iOS.
- **Language:** Objective-C with manual retain and release, as the
  macOS backend (mwin-0024). It links UIKit and QuartzCore.
- **The loop:**
  - `mwinRun` leaves the context in the main thread's dictionary, where
    the library's application delegate takes it (the library keeps no
    global), and calls `UIApplicationMain` with the process's own
    arguments. It does not return.
  - The delegate gives every scene the library's scene delegate, so the
    program's Info.plist carries a `UIApplicationSceneManifest` and
    names no class.
  - The program's init runs when the first scene connects, so its
    windows have a scene; frames then come from a display link on the
    main run loop in its common modes, once a scene is in the
    foreground (a scene the system connects ahead, prewarming, runs
    none).
  - A process without a bundle identifier (a tool, not a launched
    application) cannot be an application: the backend refuses it with
    `mwin_errorPlatform`, where UIKit would wait for it forever.
  - The frame that stops the program, and the application's end, call
    quit and free the context. The library never ends the process: the
    application runs on without frames until the system ends it, or the
    program does from quit.
- **The life cycle:** when a scene leaves the foreground and no other
  is in it, the program hears `mwin_eventSuspending` and runs a frame of
  its own at once, as on the web, then `mwin_eventSuspended` is posted
  and the frames pause; the first scene back posts `Resuming`, runs a
  frame, posts `Resumed`, and the frames go on. Lifecycle records
  coalesce, so the suspended record, still waiting when the
  application resumes, gives way to the resuming one. A scene only
  resigning active (a notification, the Control Center pulled down)
  stays running: the program is still seen.
- **Windows:**
  - A window is a scene's `UIWindow`, whose root view controller shows
    the library's view, backed by a `CAMetalLayer` at its screen's
    scale. The status bar is hidden, the window being the program's to
    draw; the safe area says what the system still covers
    (`mwin_eventSafeAreaChanged`).
  - A new window takes the scene that waits for one: the one connected
    at launch, or one a destroyed window left. Without one it fails;
    more scenes (iPadOS) come later.
  - The system sizes and places scenes: a window's size is its scene's,
    its place on its screen, and its mode borderless full screen while
    it covers the screen, windowed otherwise (split view, Stage
    Manager). Size, place, mode and style requests are unsupported.
  - The title is the scene's, which the app switcher shows. Visibility
    hides or shows the window; focus makes it the key window.
  - A scene the system lets go asks the program to close its window.
- **Touches, the Pencil and the pointer** come to the view:
  - A finger is a touch by the UITouch's address, stable over its life,
    its pressure its force where the device measures it, else -1.
  - The Pencil is a pen touching the screen: its force over the most
    it can be is its pressure, and its altitude and azimuth its tilt
    toward x and y (the pointer events' conversion). It has no eraser
    end and no barrel button, and its hover is left for later.
  - A mouse or trackpad on an iPad is the cursor. Its clicks are touches
    of the indirect pointer type where the program's Info.plist sets
    `UIApplicationSupportsIndirectInputEvents` (else they come as
    fingers), the buttons held from the event's button mask, whose bits
    are the contract's, the click count from the tap count and the
    modifiers from the event's flags. Its motion without a button comes
    through a hover recognizer taking the pointer only, entering and
    leaving; its scrolling through a pan recognizer taking scroll events
    only, ten points to a detent, with macOS's signs.
- **Keys, text and the on-screen keyboard:**
  - A hardware keyboard's presses reach the view controller (iOS 13.4),
    which passes them on to UIKit after posting them. A UIKey's code is
    the USB HID usage, the contract's key code; its meaning the
    character it types without modifiers, lower-cased, or
    `MWIN_KEY_NAMED` with the code. UIKit repeats no press. iOS cannot
    be asked what a layout's key types, so a printing key's meaning is
    known once pressed under the current layout (0 before), and the
    input mode's change forgets them and posts the layout's change.
    The layout's name is the input mode's primary language.
  - The view is a `UIKeyInput` client and the first responder while its
    window shows, so text is typed into it whether or not the window
    accepts text, as on the other platforms. A newline, a tab and a
    deletion that come without a press (the on-screen keyboard's) are
    the Enter, Tab and Backspace keys' press and release; one after a
    hardware key's press is not posted twice.
  - The on-screen keyboard shows while the program asks for it: the
    view's input view is otherwise an empty view, which keeps it away.
    The purpose sets its type (a decimal pad for numbers, the e-mail and
    URL keyboards) and secure entry for passwords; nothing is
    corrected, capitalized or completed, and a hardware keyboard shows
    no shortcuts bar. The part of each window it covers is posted when
    its frame changes.
  - Input methods' compositions (`UITextInput`) come in a later slice.
- **Monitors** are the screens the connected scenes show on, the
  application's own first and primary, external displays to its right.
  Bounds are in pixels at the screen's scale in the interface's
  orientation, the work area the whole screen; a screen above 60 Hz is
  ProMotion's, whose rate varies. iOS tells no physical size.
- **Native handles** give the view and its layer.
- **Tests** run in the simulator: a toolchain file
  (`cmake/ios-simulator.cmake`) builds for it and runs every executable
  test through `simctl spawn`; UIKit tests are applications, run by
  `tools/run_ios_app.sh`. It installs and launches one, naming in its
  environment the file its output goes to (simctl's own redirection
  gives nothing on a CI runner), waits a minute at most for the closing
  line its quit prints, since `simctl launch` returns once the
  application runs, and ends it; a failed run shows where the
  application is stuck (`sample`) or its crash report. CI boots an
  iPhone of the newest runtime and launches a system application once
  first, a new device's first launch taking minutes.
- **Slices:** the first the loop, scenes, windows, screens, the life
  cycle and native handles; the second touch, the Pencil, hardware
  keys, text and the on-screen keyboard, the pointer, then input
  methods; the third
  gamepads; the fourth the clipboard, drops, the document picker,
  alerts, addresses and keeping the display awake; the fifth system
  facts, locales and accessibility.

## Consequences

Programs open windows on iOS with Metal layers, their frames following
the display. A program learns it is going to the background in a frame
it runs at once, and draws nothing until it comes back. The first
window of a program is its launch scene's; a program on an iPhone has
one window.

- `ios` runs in the simulator: a window on the launch scene, its view
  and layer, its size, scale, safe area and mode as UIKit lays it out,
  the screen as the primary monitor; the title as the scene's, a size
  request unsupported, hiding and showing; the application going to the
  background and back as UIKit tells the scene's delegate, from outside
  the frames, with no lifecycle record at launch, suspending told in a
  frame of its own and no frames while suspended; a destroyed window's
  scene taken by the next window. A real trip to the background (the
  home screen) is not made: simctl has no way to send an application
  there.
- `ios_input` hands the view what UIKit would, which the simulator
  cannot make: its own touches for a finger's down, move and up and
  another's cancel, the Pencil at half its force tilted 45 degrees
  toward x, the pointer's double click with Shift and a second button;
  its own recognizers for the pointer's hover and scrolling, through the
  actions the view gives them.
- `ios_keys` hands the controller a hardware keyboard's presses, Shift
  and A, the left arrow, Return with the newline UIKit then types; types
  into the view as the on-screen keyboard would, text with a tab, and a
  deletion; asks for the keyboard for a number and tells the view of its
  frame, then hides it. The press path from a real keyboard through
  UIKit's text system, and the keyboard itself, are not seen: the
  simulator on CI has neither.
- Every other test runs in the simulator too, through `simctl spawn`,
  on the test backend.
