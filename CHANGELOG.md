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
  wheel in high-resolution steps) and touch.
