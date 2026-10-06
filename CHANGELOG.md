# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Added

- Keys the platform keeps (mwin-0031): `mwinGetKeyReach` tells whether
  a chord, a key with modifiers, is delivered, shared with the
  platform, uncertain or never delivered, from a table per platform;
  on the web Chromium's own chords never arrive. `mwinTestSetKeyReach`
  sets the test backend's answers.

- Input methods on X11 (mwin-0030): IBus, through its portal, and
  Fcitx 5 over the session bus, Fcitx first when `XMODIFIERS` names
  it. While a focused window accepts text, keys go to the input method
  and are held in order until it answers, at most 100 ms; a key it
  takes is dropped, compositions arrive as `mwin_eventImePreedit` and
  committed text as `mwin_eventTextInput`. The candidate window follows
  the caret.

### Fixed

- X11 reports monitors set or deleted through RandR (`xrandr
  --setmonitor`, a desktop splitting a wide display), which the X
  server tells only as the root window's ConfigureNotify. Platform
  tests now plug monitors in and out on Wayland (a `wl_output` of the
  test compositor) and X11 (a RandR monitor on the X server).

## [0.6.0] - 2026-10-06

The requirements' optional parts: cursors from images, trigger rumble
and motion for gamepads, and clipboard data by MIME type with the
primary selection.

### Added

- Cursors made from images (mwin-0027): `mwinCreateCursor` takes up to
  four RGBA images of one cursor, for scale 1 and higher scales, at
  most 128 pixels a side, with a hotspot; `mwinRequestCursorImage`
  shows one over a window and `mwinDestroyCursor` ends it. The
  context's `cursors` limit, default 16, bounds them. Every backend
  but iOS serves them: X11 through RENDER, whose development files the
  build now names; Wayland as shared memory on a surface a viewport
  sizes; the web as PNG in a CSS `image-set()`; macOS as an NSCursor
  with a representation per image; Android as a `PointerIcon`.
- Android serves cursor shape requests, as the system's `PointerIcon`
  of each shape.
- Trigger rumble and motion as optional gamepad capabilities
  (mwin-0028): `mwin_padTriggerRumble` and
  `mwinSetGamepadTriggerRumble` run the triggers' motors apart from the
  grips'; `mwin_padMotion`, `mwinSetGamepadMotion` and
  `mwinGetGamepadMotion` read the latest acceleration and rotation rate
  and the angle turned since the last read, summed over every sample.
  Windows.Gaming.Input grants trigger rumble to Microsoft's pads from
  the Xbox One on, and the web to pads whose actuator has
  `trigger-rumble`, and GameController to pads whose haptics reach
  their triggers; Linux grants motion to pads whose driver makes a
  motion sensors device (hid-playstation, hid-nintendo), and
  GameController to pads with a gyroscope (GCMotion), and Android 12
  and later to pads with an accelerometer and a gyroscope; the test
  backend gives both.
- Clipboard data by MIME type and the primary selection (mwin-0029):
  `mwinRequestClipboardWriteData` offers up to four typed items, one of
  them text, their bytes passed through unconverted;
  `mwinRequestClipboardReadData` and `mwinGetClipboardData` read one
  type back; `mwinRequestPrimaryWrite`, `mwinRequestPrimaryRead` and
  `mwinGetPrimaryText` keep the selected text apart from the clipboard.
  X11 offers each type as its own target, large items in pieces
  (INCR), and owns PRIMARY apart from CLIPBOARD; Wayland offers each
  type on its data source and keeps the primary selection through its
  protocol where the compositor has it; Win32 registers a format per
  type ("PNG" and "HTML Format" for theirs); macOS and iOS put each type
  on the pasteboard as UTType names it; the web writes a ClipboardItem,
  a type other than text/plain, text/html and image/png as a web custom
  format, and the browser may encode an image again; the test backend
  serves them too. The primary selection is X11's and Wayland's alone;
  Android answers both `mwin_outcomeUnsupported`.

### Fixed

- `MWIN_NODISCARD` is `[[nodiscard]]` under MSVC's C++17 compiler too,
  which keeps `__cplusplus` at 199711L without `/Zc:__cplusplus`; CI
  compiles every public header with MSVC as C17 and C++17.

## [0.5.0] - 2026-10-01

The Android backend, the whole contract on NativeActivity with the
library's Java, its tests in the Android emulator.

### Added

- The Android backend's first part (mwin-0026): the library's Java
  activity (`maul.window.Activity`, a NativeActivity), the program's
  entry `mwinAndroidMain`, the loop on the main thread with the
  choreographer's frames, the life cycle, activities made anew joining
  the running program, the window and its surfaces, and native handles
  with the activity; `cmake/android-emulator.cmake` and the emulator
  runners, with which CI runs every test in the Android emulator.
- Android input (mwin-0026): touches, the pen, the mouse's cursor,
  buttons and wheel, and keys with their meanings and text, from the
  activity's input queue; Android's system keys stay the system's.
- Android input methods, the on-screen keyboard and insets (mwin-0026):
  the library's activity gives input methods an editor that holds only
  the composition, compositions are told while the window accepts text,
  the keyboard shows for a purpose, and the window, drawn behind the
  system's bars, tells its safe area and the part the keyboard covers.
- Android gamepads (mwin-0026): gamepads and joysticks listed through the
  library's Java helper, mapped by place as SDL maps them where Android
  names a south face button, raw otherwise, with rumble and batteries
  from Android 12.
- Android services (mwin-0026): the clipboard's text, addresses opened
  in the user's application, the display kept awake; no file manager
  and no message box on Android.
- Android file dialogs (mwin-0026): the document picker opens documents,
  copied into the application's cache between frames and answered as
  their copies' paths; saving and folders are unsupported on Android.
- Android drops (mwin-0026): drags over the window told as they move, and
  a drop's documents copied into the cache before it is told, with its
  text.
- Android facts and Back (mwin-0026): the theme, text scale, reduced
  motion, accent, power and preferred locales; Back is the window's close
  request.
- Android accessibility (mwin-0026): the program's
  `AccessibilityNodeProvider` as the root, explored by touch through
  `maul.window.Explorer`; the activity's view in the native handles.

## [0.4.0] - 2026-10-01

The iOS backend, the whole contract on UIKit with scenes, its tests in
the iOS simulator.

### Added

- The iOS backend's first part (mwin-0025): UIKit's loop and the scene
  life cycle, windows on scenes with CAMetalLayer views, their sizes,
  scales, safe areas and modes, the screens as monitors, the
  application's suspension told in a frame of its own, and native
  handles. A toolchain file builds for the iOS simulator, where every
  test runs.
- iOS touches, the Pencil as a pen, and an iPad's mouse or trackpad as
  the cursor, its buttons, hover and scrolling (mwin-0025).
- iOS keys from a hardware keyboard, typed text, and the on-screen
  keyboard with its purpose and the part of the window it covers
  (mwin-0025).
- iOS input methods' compositions, through the view as a text input
  client (mwin-0025).
- iOS gamepads and their motors through GameController and CoreHaptics,
  with the code macOS now shares (mwin-0025).
- iOS clipboard, addresses opened, the display kept awake, and message
  boxes as alerts (mwin-0025).
- iOS drag and drop of text and files, the files copied for the program
  (mwin-0025).
- iOS file dialogs through the document picker: files opened as copies,
  folders, and saves by exporting a file to the place chosen
  (mwin-0025).
- iOS system facts, preferred languages and accessibility hooks
  (mwin-0025).

## [0.3.0] - 2026-09-30

The macOS backend, the whole contract on AppKit, and the tracking of
runtime-listed gamepads shared between Windows and macOS.

### Added

- The macOS backend's first slice: AppKit windows whose views hold a
  CAMetalLayer, the screens as monitors, and frames from the screen's
  display link, or a timer before macOS 14 (mwin-0024). Input and the
  rest are unsupported until later slices.
- macOS keys, text, mouse and wheel: keys by virtual key code with the
  layout's meaning and its changes, typed text through a text input
  client, AppKit's click counts, and precise scrolling at ten points to
  a detent (mwin-0024).
- macOS input methods, cursors and capture: compositions with their
  clauses while a window accepts text, the system cursors with blank
  ones for the hiding modes, and a captured cursor's motion as deltas
  while the window has focus; confinement is unsupported (mwin-0024).
- macOS pens: a tablet's pen as pen records rather than the mouse, with
  pressure, tilt, the barrel button and the eraser end (mwin-0024).
- macOS gamepads through GameController: every extended gamepad,
  mapped, with its name and battery, read in the background too, and
  their motors through CoreHaptics (mwin-0024).
- The macOS clipboard and services: text on the general pasteboard,
  addresses opened by NSWorkspace, files shown in the Finder, and the
  display kept awake by a power assertion (mwin-0024).
- macOS drag and drop: files and text dropped on a window, with drags
  entering, moving and leaving (mwin-0024).
- macOS file dialogs as sheets that do not block, with a menu of
  filters, and the message box as an alert (mwin-0024).
- macOS system facts and locales: the appearance, the accent color,
  reduced motion, the power source, Low Power Mode and the preferred
  languages, read again when they change (mwin-0024).
- macOS owned windows and popups as child windows, and window icons as
  the application's icon (mwin-0024).
- macOS chrome: styles, custom chrome with hit regions (caption drags
  and double clicks, edge resizes), size limits, aspect ratios and
  opacity (mwin-0024).
- macOS accessibility hooks: the program's NSAccessibility root as the
  window view's child, and the first client's question posted
  (mwin-0024).

### Changed

- The pads Windows.Gaming.Input lists are tracked by a tracker shared
  with macOS's GameController pads (mwin-0023).
- A build of the library alone on macOS targets macOS 11 unless told
  otherwise (mwin-0024).

### Fixed

- A step of the program taken while its own code runs (a modal panel
  spinning the platform's loop) no longer runs a frame inside a frame. Its tracking is Windows.Gaming.Input's, now
  shared (mwin-0023).

## [0.2.0] - 2026-09-30

The web backend without Emscripten, Xbox gamepads through
Windows.Gaming.Input, a misuse count per context, and checked sizes
throughout.

### Added

- The web backend without Emscripten: a wasm32-wasi build imports the
  backend's JavaScript, which the build writes to `maul-window.mjs` from
  the same `EM_JS` functions (`tools/gen_web_glue.py`), and `mwinRun`
  returns once init has succeeded while the page's frames run the
  program (mwin-0022), so the context keeps a copy of the application
  def rather than the caller's. A toolchain file,
  `cmake/wasm32-wasi.cmake`, and a CI job run every test that way, the
  browser tests included.
- Xbox gamepads on Windows through Windows.Gaming.Input, where the
  system has it: any number of pads, with their names, USB ids and
  batteries, and a battery's change posted (mwin-0023). XInput reads
  them where the runtime is missing.
- The context's misuse count (`mwinGetContextMisuse`): every call a
  live context refuses as invalid input counts one, as the family's
  conventions require; stale ids and a NULL context count nothing.

### Changed

- The page's JavaScript functions all carry the library's prefix, so
  they meet no program's own in the module they share.
- The context's block, each backend's platform block and every
  allocation sized from a count are laid out with checked arithmetic,
  as the family's conventions require; a size past `size_t`, which the
  32-bit web build can reach with large limits, is refused with
  `mwin_errorCapacity`.

### Fixed

- The critical ring's capacity was 16 bits and wrapped to 0 at 65535
  windows; ring capacities are 32-bit.
- A Wayland clipboard or drop read under a limit of `UINT32_MAX`
  wrapped its growth bound to 0 and read nothing.

## [0.1.0] - 2026-09-29

The first release: the whole contract on the Win32, Wayland, X11 and
web backends, with a headless test backend for programs' own tests.

### Added

- The library skeleton: the build, the family rules and tools, the
  version and result API (`mwinGetVersion`, `mwinResultName`) and the
  library profile.
- The context and the run function (`mwinRun` with init, frame and
  quit), windows as ids with requests answered by exactly one
  completion (`mwinCreateWindow`, `mwinDestroyWindow`,
  `mwinGetWindowState`, `mwinRequestTitle`, `mwinRequestSize`,
  `mwinRequestPosition`, `mwinRequestMode`, `mwinRequestVisible`,
  `mwinRequestFocus`), and the ordered event stream (`mwinNextEvent`),
  with the named limits (mwin-0002).
- Input: keys by physical code and layout meaning, validated text,
  cursor, buttons, wheel, raw deltas, touch and pen records, in four
  bounded classes per window with merging and input state resets
  (mwin-0004); cursor mode and shape requests (`mwinRequestCursorMode`,
  `mwinRequestCursorShape`); `mwinMapKeyCode` and
  `mwinGetKeyboardLayout`.
- The application lifecycle (suspending, suspended, resuming, resumed)
  and surface loss, delivered before every other record, with a frame
  run at once from inside the platform's call when it waits for the
  program; suspending resets every window's input.
- Monitors: ids stable while connected, hotplug and change records,
  facts including HDR luminances and the SDR white level
  (`mwinGetMonitors`, `mwinGetMonitorInfo`), and the window's monitor
  with `mwin_eventDisplayChanged`.
- System facts: the theme, accent color, reduced motion, text scale
  and power (`mwinGetSystemFacts`), the preferred locales
  (`mwinGetPreferredLocales`), their change records and the keyboard
  layout's; a window's safe area, and the on-screen keyboard with
  `mwinRequestVirtualKeyboard` and the part it covers.
- Input methods: `mwinRequestTextInput` with the caret rectangle,
  compositions with their caret, selection and styled segments
  (`mwin_eventImePreedit`), commits as text, and the character keys a
  composition consumes left out with their releases.
- Window properties: size limits, aspect ratio, style (resizable,
  decorated, always on top) and opacity requests, and the native handle
  bundle of each surface generation for a GPU layer
  (`mwinGetNativeHandles`, `native.h`).
- The headless test backend (`MAUL_WINDOW_TEST_BACKEND`, `test.h`) and
  the contract tests against it.
- Maul Unicode 0.2.0 as a dependency, found installed or fetched
  (mwin-0003).
- The Wayland backend (`MAUL_WINDOW_WAYLAND`, on by default on Linux),
  the native backend when `WAYLAND_DISPLAY` is set: `libwayland-client`
  opened at run time, xdg-shell toplevels, server-side decorations,
  fractional scaling through the viewporter, outputs as monitors, and
  the title, size, size limit and mode requests (mwin-0005); the
  keyboard through `libxkbcommon`, opened at run time: key codes from
  evdev, their meaning in the current layout, text with compose
  sequences, modifiers, key repeat, and layout changes; the pointer
  (motion by pointer frame, buttons with quick clicks counted, the
  wheel in high-resolution steps) and touch; cursor shapes through the
  cursor shape protocol or the cursor theme, hidden, captured (a
  locked pointer with raw deltas) and confined cursors; input methods
  through text-input-v3; and a frame of its own where the compositor
  draws none (mwin-0005): a caption that moves the window, with close,
  maximize and minimize buttons, and resize margins.
- The X11 backend (`MAUL_WINDOW_X11`, on by default on Linux), the
  native backend where `DISPLAY` is set and Wayland is not reachable:
  XCB opened at run time, windows with their ICCCM and EWMH
  properties, the scale from `Xft.dpi`, modes through the window
  manager, and monitors from RandR (mwin-0006); the keyboard through
  XKB and xkbcommon-x11, the core pointer and wheel, cursor shapes
  through libxcb-cursor, hidden and confined cursors, and captured
  cursors with XInput 2 raw motion.
- The Win32 backend (`MAUL_WINDOW_WIN32`, on by default on Windows):
  per-monitor DPI awareness version 2, windows at their logical size,
  sizes, places, focus and modes from the window procedure, borderless
  full screen, size limits and aspect ratio, frames during Windows'
  size-move loop, and monitors with their DPI and refresh rate
  (mwin-0007); the keyboard by scan code with meanings from the
  layout and text from `WM_CHAR`, the mouse with Windows' double-click
  time and distance, the wheels, cursor shapes, hidden and confined
  cursors, and captured cursors with raw input; touch and pen from the
  WM_POINTER messages, with pressure, tilt and the barrel button; input
  methods through imm32, compositions with their clauses and caret;
  system facts (theme, accent, text scale, reduced motion, power) and
  preferred languages, and title bars that follow the dark theme.
- The web backend (`MAUL_WINDOW_WEB`, on by default with Emscripten):
  canvases as windows, the page's own or made by the library, their
  CSS sizes and device pixels, the drawing buffer following the box,
  devicePixelRatio as the scale, fullscreen through the Fullscreen API,
  the screen as the monitor, the color scheme, reduced motion and
  navigator.languages, and a loop the browser drives (mwin-0008). A
  window def names a page's canvas by its selector. The keyboard by
  KeyboardEvent.code with meanings from the layout map, text, the
  mouse, the wheel, touch and pen from pointer events, CSS cursors,
  and a captured cursor through pointer lock with raw motion; input
  methods and on-screen keyboards through a text field at the caret;
  the lifecycle from page visibility, the back-forward cache and
  freezing, with the program's frame run inside the browser's event,
  and surfaces lost with a canvas out of the document.
- The gamepad contract (`MAUL_WINDOW_GAMEPAD`, on by default,
  mwin-0009): mapped gamepads by the standard location model and raw
  ones by number, hotplug and battery records, button and axis records
  in the stream, the state as last reported, and rumble; the test
  backend's gamepads and the contract tests. Linux gamepads through
  evdev for the Wayland and X11 backends, mapped by SDL_GameControllerDB
  (compiled into tables) or the kernel's gamepad layout, with rumble
  through force feedback. Win32 gamepads through XInput, with rumble
  stopped when its duration runs out. Web gamepads through the Gamepad
  API, the standard mapping mapped, with dual-rumble.
- The clipboard (mwin-0010): UTF-8 text written and read through
  requests of a window (`mwinRequestClipboardWrite`,
  `mwinRequestClipboardRead`), the text read copied out with
  `mwinGetClipboardText`, ill-formed text repaired, the
  `clipboardBytes` limit and `mwin_outcomeTooLarge`; the test backend's
  clipboard and the contract tests. The Win32 clipboard, as
  `CF_UNICODETEXT`; the web clipboard, through the Clipboard API; the
  Wayland clipboard, through the seat's data device; the X11
  clipboard, through the CLIPBOARD selection with INCR transfers.
- Drag and drop (mwin-0011): drag records while something is dragged
  over a window, drops of files and text with their payload copied out
  under the drop's number (`mwinGetDroppedFiles`,
  `mwinGetDroppedText`), the `droppedFiles` and `dropBytes` limits; the
  test backend's drops and the contract tests. Web drag and drop on
  each canvas; Win32 drag and drop through an OLE drop target, and
  WM_DROPFILES where OLE cannot start; Wayland drag and drop through the
  seat's data device, files from text/uri-list; X11 drag and drop by
  XDND 5.
- Platform services (mwin-0012): requests to open a web or mail
  address (`mwinRequestOpenUrl`), reveal a file
  (`mwinRequestRevealFile`) and keep the display awake
  (`mwinRequestKeepAwake`, the window state's `awake`); a message box
  that needs no context (`mwinShowMessageBox`), through `MessageBoxW`
  on Win32, `alert` and `confirm` on the web, and zenity or kdialog run
  without a shell on Linux; the test backend's services and the
  contract tests. Win32 services through ShellExecuteExW,
  SHOpenFolderAndSelectItems and the thread's execution state. Web
  services through window.open and a wake lock. Linux services: xdg-open
  for addresses, the file manager over the session bus (libdbus-1,
  opened at run time) for files; keeping awake by Wayland idle
  inhibitors, else over the bus through the screensaver or the portal.
- File dialogs (mwin-0013): `mwinRequestFileDialog` to open one file or
  many, save one or choose a folder, with extension filters; the paths
  copied out under the request (`mwinGetDialogFiles`); the
  `dialogFiles` and `dialogBytes` limits; cancelled for a dialog the
  user closes; the test backend's dialogs and the contract tests.
  Linux dialogs through the desktop portal's FileChooser, else zenity;
  Win32 dialogs through the common item dialog, frames going on while
  it shows.
- Window icons (mwin-0014): `mwinRequestIcon` with up to four RGBA
  images; Win32 big and small icons, X11 `_NET_WM_ICON`, Wayland
  xdg-toplevel-icon-v1 (vendored); the test backend's icons and the
  tests.
- Owned and popup windows (mwin-0015). The window def gains `owner`,
  `kind` and `position`:
  - Owned windows stay in front of their owners and are destroyed with
    them.
  - Menus and tooltips are placed against the owner's content and
    follow it.
  - A menu that loses the keyboard is asked to close.
  - Win32 uses owned windows and `WS_POPUP` tool windows. X11 uses
    `WM_TRANSIENT_FOR` and override-redirect popups. Wayland uses
    `set_parent`, and `xdg_popup` with a positioner, a grab and
    reposition. On the web, popups are unsupported.
- Custom chrome (mwin-0016): the `mwin_styleCustomChrome` style and
  `mwinRequestHitRegions`, whose typed regions (caption, edges and
  corners, buttons) the core hit-tests:
  - A press on a caption or an edge moves or resizes the window
    through the platform, and a double click on a caption maximizes it.
  - Windows 11 snap layouts appear over a maximize region
    (`mwinSystemFacts.snapLayouts`).
  - Win32 uses `WM_NCCALCSIZE` and `WM_NCHITTEST`, X11
    `_NET_WM_MOVERESIZE`, and Wayland `xdg_toplevel.move` and `resize`
    with client-side decorations. On the web, hit regions are
    unsupported.
- Accessibility hooks (mwin-0017), for the program's adapters, which
  own the tree:
  - `mwinRequestAccessibilityRoot` hands the platform the root. On
    Win32 it is an `IRawElementProviderSimple*`, answered to
    `WM_GETOBJECT` through UI Automation.
  - `mwin_eventAccessibilityRequested` comes when a client first asks
    for a window's tree.
  - On the web, a host element over the canvas takes the ARIA
    elements; its selector is in `mwinNativeHandles`.
  - The test backend's `mwinTestAskAccessibility` plays a client
    asking.
- A sample (`samples/window.c`): one window and what the platform says
  of it, fullscreen, text input with input methods, and a clean end.
- Linux preferred locales from the environment (`LANGUAGE`, then the
  messages locale), as BCP 47 tags, on X11 and Wayland.
- Linux look and motion from the desktop portal's settings (theme,
  accent, reduced motion, text scale), and power from the portal and
  UPower (low power, on battery), followed as they change (mwin-0018).
- Wayland focus requests through xdg-activation, and the launcher's
  activation token for the first window shown (mwin-0019).
- X11 smooth scrolling from XI 2.1 scroll valuators, with the pointer
  read through XI2 (mwin-0020).
- Windows generic gamepads through Raw Input and the HID parser, mapped
  by SDL_GameControllerDB's Windows entries (mwin-0021).
- Fuzz targets (`-DMAUL_WINDOW_FUZZ=ON`) for the bytes other programs
  and devices hand the library: drop and clipboard payloads, URI lists,
  zenity's output, the environment's locales, scroll valuators and
  gamepad controls, run in CI. The first run found a stick driven by an
  axis and a button at once going past its range; it is now held to it.
