# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and the behavior.

## [0.2.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.2.0) (2026-10-25)

### Added

- On X11, a window wholly covered by other windows is reported
  occluded (`mwin_eventOccluded`), and revealed (`mwin_eventRevealed`)
  once any part of it can be seen again, as the X server computes it;
  `mwinGetWindowState` says the same. Under a compositing window
  manager the X server reports no window covered, so there a window is
  never occluded. The windows guide now lists when each platform
  reports occlusion
  ([#107](https://github.com/Rawframe-Project/maul-window/pull/107)).

## [0.1.1](https://github.com/Rawframe-Project/maul-window/compare/v0.1.0...v0.1.1) (2026-10-25)

### Fixed

- On Windows, input records (keys, typed text, the mouse, the wheel,
  raw input, touch and pen) carry the time their message happened, as
  `timeNs` promises, instead of the time the library handled it: input
  that waited behind a long frame or a modal loop is no longer stamped
  late by that wait. Touch and pen take the time Windows recorded with
  the sample. Input times never go backwards, across all kinds of input
  ([#102](https://github.com/Rawframe-Project/maul-window/pull/102)).

## [0.1.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.1.0) (2026-10-24)

### Changed

- `mwinMessageBoxDef` carries the ABI version, like the application
  def, and `mwinShowMessageBox` refuses a def from other headers with
  `mwin_errorVersion`. A program must rebuild against the new headers
  and build its def with `mwinDefaultMessageBoxDef()`, which now sets
  the version; a def filled in field by field must also set
  `version = MWIN_ABI_VERSION`
  ([#31](https://github.com/Rawframe-Project/maul-window/pull/31)).
- On iOS, a window created when no scene is free completes
  `mwin_outcomeUnsupported` where the application cannot show several
  scenes (an iPhone, or an Info.plist without
  `UIApplicationSupportsMultipleScenes`), instead of
  `mwin_outcomeFailed`
  ([#69](https://github.com/Rawframe-Project/maul-window/pull/69)).

### Added

- iPadOS windows: each further window gets a scene of its own, which
  the system shows side by side or in Stage Manager. Creating one can
  take up to ten seconds, and completes `mwin_outcomeFailed` if the
  system refuses or no scene comes. A scene the user closes asks its
  window to close, and a window the program destroys while others remain
  closes its scene
  ([#69](https://github.com/Rawframe-Project/maul-window/pull/69)).
- A hovering Apple Pencil is reported on iPadOS as pen motion without
  contact, with its tilt on iOS 16.4 and later, as the other platforms
  report a hovering pen
  ([#62](https://github.com/Rawframe-Project/maul-window/pull/62)).
- On Linux the preferred locales follow the desktop while the program
  runs: a language changed in GNOME's settings or in KDE Plasma's
  `plasma-localerc` replaces the list, and `mwin_eventLocaleChanged`
  says so. The list still starts from the program's environment, and
  without a system bus or a configuration folder it simply stays
  ([#67](https://github.com/Rawframe-Project/maul-window/pull/67)).

### Fixed

- On Linux a desktop portal or power service answering with values of
  an unexpected type no longer ends the program, and a text scale that
  is infinite or out of range no longer reaches it
  ([#32](https://github.com/Rawframe-Project/maul-window/pull/32)).
- On X11 a key held for the input method's answer reaches only the
  window it was pressed in, not a new window made in its place
  ([#34](https://github.com/Rawframe-Project/maul-window/pull/34)).
- On the web, records, strings, drops and clipboard text from the page
  are checked and repaired before the program sees them, so another
  script on the page can no longer crash the program or hand it
  positions, pressures, sizes or codes outside the contract
  ([#41](https://github.com/Rawframe-Project/maul-window/pull/41)).
- On Android 11 the on-screen keyboard asked for as an activity starts
  now shows, rather than depending on when input starts on the view
  ([#47](https://github.com/Rawframe-Project/maul-window/pull/47)).
- On Wayland a minimize that supersedes a waiting maximize or
  fullscreen request no longer lets a later configure answer another
  request with that mode's outcome
  ([#56](https://github.com/Rawframe-Project/maul-window/pull/56)).
- On iOS destroying a window whose creation failed no longer crashes,
  and a scene the system closed is never given to the next window
  ([#69](https://github.com/Rawframe-Project/maul-window/pull/69)).

## [0.0.1](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.0.1) (2026-10-23)

The first release: windows, input, gamepads and platform services
behind one contract on Windows, macOS, iOS, Android, Wayland, X11 and
the web, with a headless test backend for programs' own tests.

### Added

- A program model in which every change to a window is a request
  answered by exactly one completion, everything the platform reports
  arrives in one ordered stream, no library thread runs, and all memory
  is taken when the context is created within named limits.
- Windows with their modes, styles, size limits, aspect ratio, opacity
  and icons; owned windows, menus and tooltips; custom chrome with hit
  regions that move and resize through the platform, and Windows 11's
  snap layouts; monitors with their scale, refresh rate, variable
  refresh and HDR facts; the native handles a GPU layer needs, and lost
  and restored surfaces on Android and the web.
- Keys by physical position and by the layout's meaning, which chords
  the platform keeps, text and input method compositions (IBus and
  Fcitx 5 on X11), the on-screen keyboard with an input purpose, mouse,
  wheel, raw motion and pointer capture, system and image cursors,
  touch, and pens with pressure, tilt, eraser and barrel button.
- Gamepads mapped by place through the platform or SDL_GameControllerDB,
  or raw, with batteries, rumble, trigger rumble and motion sensors;
  Windows.Gaming.Input, XInput and HID pads on Windows, evdev on Linux,
  GameController on macOS and iOS, Android's input devices and the
  Gamepad API.
- The clipboard (text, data by MIME type, the primary selection on X11
  and Wayland), drag and drop of files and text, file dialogs, message
  boxes (including one drawn on X11 where neither zenity nor kdialog is
  installed), opening addresses, revealing files and keeping the display
  awake.
- The user's theme, accent, reduced motion, text scale, power source,
  power saving and preferred languages, with change notifications.
- Hooks for accessibility adapters: the platform's root on Windows,
  macOS, iOS and Android (with touch exploration), and a host for ARIA
  elements on the web.
- The web backend with Emscripten, or as plain wasm32-wasi with a
  generated JavaScript module of imports.
- An application def stamped with the version of the headers it was
  built with, which `mwinRun` refuses from another release.
- A headless test backend that plays any platform report and tells what
  a program asked of the platform.
- `java/proguard-rules.pro` for Android applications that shrink their
  code with R8 or ProGuard.
