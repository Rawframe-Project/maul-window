# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and the behavior.

## [0.6.0](https://github.com/Rawframe-Project/maul-window/compare/v0.5.1...v0.6.0) (2026-10-29)


### ⚠ BREAKING CHANGES

* **build:** Maul Window now needs Maul Unicode 0.6.0.
* **core:** refuse calls from inside the allocator with mwin_errorBusy ([#198](https://github.com/Rawframe-Project/maul-window/issues/198))
* the bounds refused with mwin_errorLimit are renamed: MWIN_ADDRESS_BYTES to MWIN_MAX_ADDRESS_BYTES, MWIN_CANVAS_SELECTOR_BYTES to MWIN_MAX_CANVAS_SELECTOR_BYTES, MWIN_CLIPBOARD_ITEMS to MWIN_MAX_CLIPBOARD_ITEMS, MWIN_CLIPBOARD_MIME to MWIN_MAX_CLIPBOARD_MIME, MWIN_CURSOR_IMAGES to MWIN_MAX_CURSOR_IMAGES, MWIN_CURSOR_SIZE to MWIN_MAX_CURSOR_SIZE, MWIN_DIALOG_FILTER_BYTES to MWIN_MAX_DIALOG_FILTER_BYTES, MWIN_DIALOG_FILTERS to MWIN_MAX_DIALOG_FILTERS, MWIN_DIALOG_TITLE_BYTES to MWIN_MAX_DIALOG_TITLE_BYTES, MWIN_HIT_REGIONS to MWIN_MAX_HIT_REGIONS, MWIN_ICON_IMAGES to MWIN_MAX_ICON_IMAGES, MWIN_ICON_SIZE to MWIN_MAX_ICON_SIZE, MWIN_MESSAGE_BYTES to MWIN_MAX_MESSAGE_BYTES, and MWIN_MESSAGE_TITLE_BYTES to MWIN_MAX_MESSAGE_TITLE_BYTES.
* calls refuse an argument past a fixed bound with a named constant with the new mwin_errorLimit (-9) instead of mwin_errorInvalid, and mwinCreateWindow refuses a title past the titleBytes limit with mwin_errorCapacity instead of mwin_errorInvalid.

### Added

* **input:** count the input purposes, and test a scope for each ([#228](https://github.com/Rawframe-Project/maul-window/issues/228)) ([6aaa247](https://github.com/Rawframe-Project/maul-window/commit/6aaa247346935b2d05c1148407af8b6a211c245b)), closes [#223](https://github.com/Rawframe-Project/maul-window/issues/223)


### Fixed

* **android:** find a gamepad Android 11 first read half merged ([#227](https://github.com/Rawframe-Project/maul-window/issues/227)) ([f3fa8dc](https://github.com/Rawframe-Project/maul-window/commit/f3fa8dc841864216cd489e62841d269cb49e124d)), closes [#226](https://github.com/Rawframe-Project/maul-window/issues/226)
* **android:** give a gamepad the motion Android finds after adding it ([#225](https://github.com/Rawframe-Project/maul-window/issues/225)) ([295e562](https://github.com/Rawframe-Project/maul-window/commit/295e562d7763e7d9136447a86c04403f3d6b5949)), closes [#220](https://github.com/Rawframe-Project/maul-window/issues/220)
* **android:** give each of a gamepad's two motors its own strength ([#224](https://github.com/Rawframe-Project/maul-window/issues/224)) ([a4a773a](https://github.com/Rawframe-Project/maul-window/commit/a4a773a99b39a893ed71d2fc3d9e57d1ce536b9e)), closes [#219](https://github.com/Rawframe-Project/maul-window/issues/219)
* **android:** stop a rumble through the vibrator that started it ([#240](https://github.com/Rawframe-Project/maul-window/issues/240)) ([6d734fd](https://github.com/Rawframe-Project/maul-window/commit/6d734fde269e983b99ce118d3e90319026a83807)), closes [#237](https://github.com/Rawframe-Project/maul-window/issues/237)
* **build:** build against Maul Unicode 0.6.0 ([#250](https://github.com/Rawframe-Project/maul-window/issues/250)) ([4298640](https://github.com/Rawframe-Project/maul-window/commit/429864009c05f20d2dd4abe56f3271d71d468202)), closes [#248](https://github.com/Rawframe-Project/maul-window/issues/248)
* **core:** end a window's composition with its input state reset ([#194](https://github.com/Rawframe-Project/maul-window/issues/194)) ([63f4852](https://github.com/Rawframe-Project/maul-window/commit/63f48525775fd95b4a1085da34fb904582c7a969)), closes [#192](https://github.com/Rawframe-Project/maul-window/issues/192)
* **core:** leave out the keys an input method takes after a reset ([#199](https://github.com/Rawframe-Project/maul-window/issues/199)) ([763470f](https://github.com/Rawframe-Project/maul-window/commit/763470f4bb92b8f67e7a99fe74ef8988e313e685)), closes [#196](https://github.com/Rawframe-Project/maul-window/issues/196)
* **core:** refuse calls from inside the allocator with mwin_errorBusy ([#198](https://github.com/Rawframe-Project/maul-window/issues/198)) ([62d3fdc](https://github.com/Rawframe-Project/maul-window/commit/62d3fdc1e95e53ec615a47dd0817df219c99a7e2)), closes [#191](https://github.com/Rawframe-Project/maul-window/issues/191)
* **ios:** take touches before a window first draws ([#197](https://github.com/Rawframe-Project/maul-window/issues/197)) ([2b01b63](https://github.com/Rawframe-Project/maul-window/commit/2b01b633adf4bdd2ae739741f00ccded6d7ddeb5)), closes [#195](https://github.com/Rawframe-Project/maul-window/issues/195)
* **linux:** add a pad turned away for want of room once there is room ([#213](https://github.com/Rawframe-Project/maul-window/issues/213)) ([94d846e](https://github.com/Rawframe-Project/maul-window/commit/94d846e9fdd1e933920ad7c655bd74e07529680c)), closes [#205](https://github.com/Rawframe-Project/maul-window/issues/205)
* **linux:** post nothing when a pad's resync cannot read its buttons ([#173](https://github.com/Rawframe-Project/maul-window/issues/173)) ([de43ab2](https://github.com/Rawframe-Project/maul-window/commit/de43ab25dbb505fb18e01e561e74aa3a8637c7f4)), closes [#171](https://github.com/Rawframe-Project/maul-window/issues/171)
* **linux:** skip a report the kernel cut and read the state again ([#170](https://github.com/Rawframe-Project/maul-window/issues/170)) ([10dae15](https://github.com/Rawframe-Project/maul-window/commit/10dae150251506403b5c67e5ea020fbfd8280bd6)), closes [#167](https://github.com/Rawframe-Project/maul-window/issues/167) [#168](https://github.com/Rawframe-Project/maul-window/issues/168)
* **linux:** time motion samples on the records' clock ([#164](https://github.com/Rawframe-Project/maul-window/issues/164)) ([22b17c1](https://github.com/Rawframe-Project/maul-window/commit/22b17c125e00e88ce71fe305f63446b55bd2d33e)), closes [#163](https://github.com/Rawframe-Project/maul-window/issues/163)
* name the bounds refused with mwin_errorLimit MWIN_MAX_* ([#186](https://github.com/Rawframe-Project/maul-window/issues/186)) ([5c22fcf](https://github.com/Rawframe-Project/maul-window/commit/5c22fcf687d5b2422ca77102ba5887874ad854c5))
* return mwin_errorLimit past a named bound ([#182](https://github.com/Rawframe-Project/maul-window/issues/182)) ([01f0057](https://github.com/Rawframe-Project/maul-window/commit/01f00572252640f9249feea6abf76867d056fd2a))
* **wayland:** repair the input method's text that is not UTF-8 ([#188](https://github.com/Rawframe-Project/maul-window/issues/188)) ([33a0919](https://github.com/Rawframe-Project/maul-window/commit/33a0919b845066177231c198d884c1d5653aeb7a)), closes [#187](https://github.com/Rawframe-Project/maul-window/issues/187)
* **win32:** add a Raw Input pad turned away for want of room later ([#208](https://github.com/Rawframe-Project/maul-window/issues/208)) ([3cad2c8](https://github.com/Rawframe-Project/maul-window/commit/3cad2c864cb2a5518de63d37c372443efe6b866a)), closes [#206](https://github.com/Rawframe-Project/maul-window/issues/206)

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
