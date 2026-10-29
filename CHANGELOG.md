# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and the behavior.

## [0.6.0](https://github.com/Rawframe-Project/maul-window/compare/v0.5.1...v0.6.0) (2026-10-29)

The device checks on real hardware are waived by the owner for releases
through 2026-10-29, so 0.6.0 ships on its other gates. The iOS check,
that a window shows no flash against the launch screen before its first
frame, runs against it in
[#254](https://github.com/Rawframe-Project/maul-window/issues/254).

### Changed

- **Breaking:** Maul Window needs Maul Unicode 0.6.0, against which
  its Android, web and Windows backends did not build. A build that
  provides Maul Unicode itself must provide 0.6.0
  ([#250](https://github.com/Rawframe-Project/maul-window/pull/250)).
- **Breaking:** an argument past a fixed bound with a named constant is
  refused with the new `mwin_errorLimit` (-9) instead of
  `mwin_errorInvalid`, and counts one misuse. The bound is checked on
  lengths and counts before any content is read. `mwinCreateWindow`
  refuses a def whose title passes the `titleBytes` limit with
  `mwin_errorCapacity`, as `mwinRequestTitle` already did. A program
  that tests for `mwin_errorInvalid` on these refusals must test for
  the new results
  ([#182](https://github.com/Rawframe-Project/maul-window/pull/182)).
- **Breaking:** the fourteen bounds refused with `mwin_errorLimit` are
  renamed with the prefix `MWIN_MAX_`, with their values unchanged and
  no aliases: `MWIN_ADDRESS_BYTES`, `MWIN_CANVAS_SELECTOR_BYTES`,
  `MWIN_CLIPBOARD_ITEMS`, `MWIN_CLIPBOARD_MIME`, `MWIN_CURSOR_IMAGES`,
  `MWIN_CURSOR_SIZE`, `MWIN_DIALOG_FILTER_BYTES`,
  `MWIN_DIALOG_FILTERS`, `MWIN_DIALOG_TITLE_BYTES`,
  `MWIN_HIT_REGIONS`, `MWIN_ICON_IMAGES`, `MWIN_ICON_SIZE`,
  `MWIN_MESSAGE_BYTES` and `MWIN_MESSAGE_TITLE_BYTES` are now
  `MWIN_MAX_ADDRESS_BYTES` and so on
  ([#186](https://github.com/Rawframe-Project/maul-window/pull/186)).
- **Breaking:** a call on a context from inside that context's
  allocator, which `mwinRequestClipboardWrite`, `mwinRequestIcon` and
  `mwinRequestFileDialog` run partway through, is refused with the new
  `mwin_errorBusy` (-10) and counts one misuse. Every public function
  that takes a non-const context refuses it. Such a call used to go
  ahead, and could destroy the window, take a request slot or drain the
  queue while the outer call was still using them
  ([#198](https://github.com/Rawframe-Project/maul-window/pull/198)).

### Added

- `MWIN_INPUT_PURPOSES` counts the input purposes: every purpose is
  below it. `mwinRequestVirtualKeyboard` refuses a purpose from it on
  ([#228](https://github.com/Rawframe-Project/maul-window/pull/228)).

### Fixed

- A window's `mwin_eventInputStateReset` ends its composition:
  `composing` goes false, and the input method's next preedit starts
  one again. After a composition that did not fit the window's text
  storage, or one lost to full storage, the program used to keep
  drawing the old one, and the
  character keys that went down were left out as a composition's keys
  until a later preedit
  ([#194](https://github.com/Rawframe-Project/maul-window/pull/194)).
- After such a reset, the character keys the input method still takes
  for its composition stay left out, and the program reads the input
  method's empty preedit when its composition ends. They used to reach
  the program as presses and releases until the input method's next
  preedit
  ([#199](https://github.com/Rawframe-Project/maul-window/pull/199)).
- On Wayland, the input method's committed and composed text that is
  not UTF-8 is repaired with U+FFFD. Committed text
  used to be lost without a trace, and such a composition dropped,
  leaving the previous one showing
  ([#188](https://github.com/Rawframe-Project/maul-window/pull/188)).
- On iOS, a window takes touches before it first draws: it is black
  from its creation. Its touches used to go to the home screen behind
  it until its first frame was presented, and always in a simulator
  without a Metal device
  ([#197](https://github.com/Rawframe-Project/maul-window/pull/197)).
- On Linux, the motion samples of a device that stamps its reports
  (`MSC_TIMESTAMP`) are timed on the records' clock, as
  `mwinGamepadMotion.timeNs` is documented. Their time used to count
  from the device's first stamp
  ([#164](https://github.com/Rawframe-Project/maul-window/pull/164)).
- On Linux, after the kernel drops a device's events, a pad and its
  motion sensors skip what is left of the cut report and read the
  device's state again. A motion axis whose change was lost used to
  keep its old value until it changed again, and a pad posted its state
  mixed with the rest of the cut report
  ([#170](https://github.com/Rawframe-Project/maul-window/pull/170)).
- On Linux, a pad whose buttons cannot be read again after such a drop,
  as when the device has gone, posts nothing for it. Every button it
  held used to be posted as released
  ([#173](https://github.com/Rawframe-Project/maul-window/pull/173)).
- On Linux and on Windows (a HID pad read through Raw Input), a
  gamepad connected while every slot of `limits.gamepads` was taken is
  added once a slot is free. It used to be added only when plugged in
  again
  ([#213](https://github.com/Rawframe-Project/maul-window/pull/213),
  [#208](https://github.com/Rawframe-Project/maul-window/pull/208)).
- On Android 12 and later, each of a gamepad's two motors runs at its
  own strength. Both used to run at the strength of one of them.
  Android 11's single vibrator still runs at the stronger of the two
  ([#224](https://github.com/Rawframe-Project/maul-window/pull/224)).
- On Android, a gamepad whose motion sensors Android finds a moment
  after adding it gains motion, told by `mwin_eventGamepadChanged`. It
  used to never offer motion, and `mwinSetGamepadMotion` answered
  unsupported
  ([#225](https://github.com/Rawframe-Project/maul-window/pull/225)).
- On Android 11, a gamepad Android first read half merged is found: the
  library reads a new device half a second after it is first listed,
  and a pad still read part way is added at its first input, with
  Android's standard gamepad axes. Such a pad used to stay invisible to
  the program for as long as it ran
  ([#227](https://github.com/Rawframe-Project/maul-window/pull/227)).
- On Android, a stop of a gamepad's rumble, asked for or made as the
  context ends, stops the motors. After Android changed the device, as
  when it found a pad's motion sensors, the stop used to return
  `mwin_success` and leave the motors running until the time first
  asked for ran out
  ([#240](https://github.com/Rawframe-Project/maul-window/pull/240)).

## [0.5.1](https://github.com/Rawframe-Project/maul-window/compare/v0.5.0...v0.5.1) (2026-10-27)

### Fixed

- On X11 and Wayland, a drag over a window the program destroys ends
  with the window: none of its records reach the window that takes its
  place, and a drop still being gathered for it is refused to its
  source instead of being delivered to that window
  ([#154](https://github.com/Rawframe-Project/maul-window/pull/154)).
- On X11 and Wayland, a drag still reported when another enters is
  reported left first, so a window no longer hears two
  `mwin_eventDragEntered` without a leave between them, as when an X11
  source ended without a leave or another program started a drag
  ([#156](https://github.com/Rawframe-Project/maul-window/pull/156)).
- A gamepad's logical ranges are computed in 64 bits: an axis whose
  device range spans all 32 bits normalizes correctly on Linux, Android
  and Windows, and a Windows HID hat with such a range reads its step
  instead of centred
  ([#159](https://github.com/Rawframe-Project/maul-window/pull/159)).

## [0.5.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.5.0) (2026-10-25)

### Changed

- **Breaking:** `mwinMonitorInfo.variableRefresh` is an `mwinTristate`
  (`mwin_yes`, `mwin_no`, or `mwin_unknown` where the platform does not
  tell) instead of a `bool`. A program testing it as a truth value must
  compare it with `mwin_yes`. `mwinTristate` is now declared in
  `base.h`. X11 tells it from `vrr_capable`, macOS 12 and later from
  whether the screen's refresh interval can vary, iOS by a refresh
  above 60 Hz, and Android 16 from `hasArrSupport`. Win32, Wayland, the
  web, older macOS and Android, and an X11 output without
  `vrr_capable` report unknown, where they used to report no
  ([#116](https://github.com/Rawframe-Project/maul-window/pull/116)).

### Added

- On Windows, a window is reported occluded (`mwin_eventOccluded`)
  while it is minimized, cloaked by DWM (as on another virtual
  desktop), while the session is locked or while the display is off,
  and revealed when none of these holds; a dimmed display counts as on.
  `mwinGetWindowState` says the same. Coverage by other windows is not
  reported, since Windows tells it nowhere
  ([#117](https://github.com/Rawframe-Project/maul-window/pull/117)).

### Fixed

- On iOS and Android, a window made where it cannot be seen starts out
  occluded: on iOS one that takes a scene already in the background, on
  Android one made while the activity is stopped and its surface is
  still there (a path the Android emulator does not reach). It used to
  say it
  could be seen until the platform next changed its state
  ([#115](https://github.com/Rawframe-Project/maul-window/pull/115)).

## [0.4.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.4.0) (2026-10-25)

### Added

- On iOS, a window is reported occluded (`mwin_eventOccluded`) when
  its scene goes to the background, and revealed
  (`mwin_eventRevealed`) when the scene comes back to the foreground;
  `mwinGetWindowState` says the same. The program hears of it in the
  frame that tells of the suspending, after the suspending record, and
  of the revealing after the resumed record. An inactive scene, under
  Control Center, the app switcher or a system alert, is still seen and
  stays revealed
  ([#110](https://github.com/Rawframe-Project/maul-window/pull/110)).

## [0.3.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.3.0) (2026-10-25)

### Added

- On Android, a window is reported occluded (`mwin_eventOccluded`)
  while its activity is stopped, and revealed (`mwin_eventRevealed`)
  when the activity starts again; `mwinGetWindowState` says the same.
  The program hears of it in the frame that tells of the suspending,
  after the suspending record, and of the revealing after the resumed
  record. A pause alone, as under a translucent activity, in split
  screen or in picture-in-picture, leaves the window revealed
  ([#109](https://github.com/Rawframe-Project/maul-window/pull/109)).

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
