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
  - Input methods compose through the view as a `UITextInput` client
    whose document is only the marked text; accepted text leaves at once
    as text records. While the window accepts text the marked text is
    `mwin_eventImePreedit`, one clause underlined, the method's
    selection as the selection and its end as the caret, and the
    candidate window goes by the program's caret. While it does not, a
    method still composes, as the user expects of their keyboard, but
    only the text it accepts is posted, as on macOS. Turning text input
    off drops a composition, UIKit told through its input delegate. iOS
    gives marked text no clauses, so there is one.
- **Gamepads** come through GameController and their motors through
  CoreHaptics, with the macOS backend's code (mwin-0024), now shared as
  `apple_pad.m` and `apple_rumble.m` over one state of the pads each
  backend holds: every pad GameController maps, named by its vendor,
  with its battery, and its motors per grip. iOS gives an application
  in the background no pad input, and runs no frames there.
- **Clipboard and services:**
  - The clipboard is text on the general pasteboard, written and read
    while the request is submitted. Reading what another application
    put there, iOS may ask the user first, and the read waits for them;
    a refusal reads as no text.
  - Addresses open through UIKit, whose answer completes the request
    later, done or failed; an answer after the program ended is dropped.
  - The display is kept awake by turning the idle timer off while a
    window that asks for it shows, from the pump.
  - There is no file manager to show a file in: revealing is
    unsupported.
  - The message box is a `UIAlertController` over the topmost view
    controller of a scene in the foreground (over a window of its own
    on a scene that shows none), waited for by running the main run
    loop, as AppKit's modal alerts do; the program's frames are not run
    inside it. Its buttons are English, as on macOS. An alert taken away
    without a button reads as closed; one never shown in five seconds
    fails. It needs a scene in the foreground, so it fails in the
    program's init, while the first scene connects.
- **Drag and drop:** each window's view has a drop interaction taking
  a drag of text or files with the copy operation, reporting a drag
  over that has moved. An item registering a type that is neither text
  nor an address (a file's own address is one) is a file, loaded as
  that type, a dynamic one for an extension the system does not know; another
  that loads as a string is text, so a text file dragged gives its
  text. (An item's provider can load even a file as a string, its
  address.) Items load asynchronously, and a file
  is handed over only while its load finishes, so each is copied into
  the application's temporary directory, a folder of its own per drop,
  under the name the system gave the file it handed over (the one its
  source suggests, or "Document" with the extension), and the drop gives
  those paths; the system empties it in time. The first
  text item is the drop's text. The drop is posted once every item has
  loaded, at the place it was made; one whose window or program went
  since is dropped.
- **File dialogs** are the document picker, presented over the top of
  its window's view controllers; it never blocks, and answers when the
  user is done.
  - Opening files, the picker copies them into the application, and the
    answer's paths are those copies, which the program reads as they
    are.
  - A folder is the folder itself, reached through a security scope the
    backend opens and keeps open while the application runs.
  - iOS has no save panel: the picker exports a file to a place the user
    chooses. The dialog exports an empty file of the offered name (or
    "Untitled"), so that place then holds it, and answers with the
    place, its security scope kept open for the program to write.
  - The filters allow every filter's types at once, the picker having
    no menu of them; none allows every item. The folder is where the
    picker starts; it shows no title.
  - A picker whose request goes (its window destroyed, the program
    stopping) is taken away at the next pump, answering nothing.
- **System facts:** the light or dark style of the first window (of
  the scenes before one), reduced motion, the text scale Dynamic Type
  gives the body text, whether the battery provides the power (the
  device's battery watched from the first scene to the backend's stop,
  its earlier setting put back; unknown where it cannot be watched, as
  in the simulator) and Low Power Mode. iOS has no accent
  color of the user's, only each application's tint, so there is none.
  Changes come from the view's traits and from notifications, each
  reading everything again. The locales are the preferred languages,
  read by the code macOS now shares (`apple_locale.m`).
- **Accessibility:** the program's root, an object of the
  UIAccessibility protocols whose container is the window's view, is
  the view's only accessibility element, held while it is the root; a
  client's first question about the view, root or none, posts
  `mwin_eventAccessibilityRequested` once, and a new root tells clients
  the layout changed.
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
- `ios_ime` works the view as an input method would, one composition
  a frame: marked text reported with its caret and selection in bytes,
  the candidate window at the program's caret; accepted as it is and as
  other text; shortened by a deletion; dropped when text input is
  turned off; composed without a record while it is off. A real input
  method on a real keyboard is not seen: the simulator on CI has none.
- `ios_gamepad` runs a program with GameController watched for half a
  second and lists its pads, each mapped and named. No pad is attached
  to the simulator; the tracking runs against a stand-in in
  `pad_tracker`, and the motors have run on no machine the tests run on.
- `ios_services` writes the pasteboard and reads it back, reads another
  writer's text, keeps the display awake and lets it sleep again, opens
  a mailto address the simulator has no program for (failed, when UIKit
  says), finds revealing unsupported, and shows two message boxes from
  a frame: one taken away (closed), one answered OK through the
  button's handler, which only a test may reach.
- `ios_drop` hands the view's drop interaction a drop session of its
  own over real item providers, a file and a string: entering, moving,
  leaving, entering again and dropping, each record at its place, the
  drop giving the file's copy with its contents and the text once they
  loaded. A drag from another application is not seen: the simulator
  cannot make one.
- `ios_dialog` finds the picker shown and tells its delegate what the
  picker would: a file chosen to open, read back; a pick of several
  cancelled; a save answered with the place chosen; a folder's picker
  taken away when its window is destroyed. The user's own choice, and
  the picker's export itself, are out of a test's reach.
- `ios_system` compares the facts and locales with what UIKit says,
  gives the window the other style and sees the theme change posted,
  and takes the accessibility root through its life: the first question
  told once before any root, the root as the view's element, held after
  the program's own reference went, let go with no root.
- Every other test runs in the simulator too, through `simctl spawn`,
  on the test backend.
