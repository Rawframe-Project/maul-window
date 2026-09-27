# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

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
  draws none (W4): a caption that moves the window, with close,
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
  each canvas.
- A sample (`samples/window.c`): one window and what the platform says
  of it, fullscreen, text input with input methods, and a clean end.
