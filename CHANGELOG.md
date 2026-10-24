# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and the behavior.

## [0.1.0](https://github.com/Rawframe-Project/maul-window/compare/v0.0.1...v0.1.0) (2026-10-24)


### ⚠ BREAKING CHANGES

* **services:** `mwinMessageBoxDef` has a `version` field after its cookie, and `mwinDefaultMessageBoxDef` is a static inline function of `services.h` rather than a library symbol. Programs rebuild against the new headers.

### Added

* **ios:** give each further window a scene of its own on iPadOS ([#69](https://github.com/Rawframe-Project/maul-window/issues/69)) ([4c7cfce](https://github.com/Rawframe-Project/maul-window/commit/4c7cfce7d8014f9caa8c64ec04c0d9c46eaac752)), closes [#7](https://github.com/Rawframe-Project/maul-window/issues/7)
* **ios:** report the Apple Pencil's hover ([#62](https://github.com/Rawframe-Project/maul-window/issues/62)) ([36d1581](https://github.com/Rawframe-Project/maul-window/commit/36d1581a589213b380da953db061d5760e8b6745)), closes [#8](https://github.com/Rawframe-Project/maul-window/issues/8)
* **linux:** follow the desktop's languages while the program runs ([#67](https://github.com/Rawframe-Project/maul-window/issues/67)) ([92c0827](https://github.com/Rawframe-Project/maul-window/commit/92c08279a5c4183e84f4242abbd77e20b07c8211)), closes [#38](https://github.com/Rawframe-Project/maul-window/issues/38)
* **services:** stamp the message box def with the ABI version ([#31](https://github.com/Rawframe-Project/maul-window/issues/31)) ([cc8fc5f](https://github.com/Rawframe-Project/maul-window/commit/cc8fc5f2c8f5fc4ddb460536d27cb88af65d1a4c)), closes [#25](https://github.com/Rawframe-Project/maul-window/issues/25)


### Fixed

* **android:** show a keyboard asked for before input starts ([#47](https://github.com/Rawframe-Project/maul-window/issues/47)) ([22cd5e8](https://github.com/Rawframe-Project/maul-window/commit/22cd5e80deea4d0a8735513bfe59223a4d3acb5a)), closes [#4](https://github.com/Rawframe-Project/maul-window/issues/4)
* **linux:** read only D-Bus values of the type expected ([#32](https://github.com/Rawframe-Project/maul-window/issues/32)) ([eb5f0aa](https://github.com/Rawframe-Project/maul-window/commit/eb5f0aada4e18f3d7e4bbac1c9e47f1d31f599b7)), closes [#11](https://github.com/Rawframe-Project/maul-window/issues/11)
* **wayland:** forget a mode request a minimize superseded ([#56](https://github.com/Rawframe-Project/maul-window/issues/56)) ([228533d](https://github.com/Rawframe-Project/maul-window/commit/228533dd9016262c4599072a71f6070c31182ed0))
* **web:** check every record and string the page hands over ([#41](https://github.com/Rawframe-Project/maul-window/issues/41)) ([5c4a764](https://github.com/Rawframe-Project/maul-window/commit/5c4a764520346ab653f529514ffb38ca4e94ccca)), closes [#12](https://github.com/Rawframe-Project/maul-window/issues/12)
* **x11:** deliver a held key only to the window it was held for ([#34](https://github.com/Rawframe-Project/maul-window/issues/34)) ([89aaf15](https://github.com/Rawframe-Project/maul-window/commit/89aaf15beb200abdfadc37a9263cf0bd5db9adf1)), closes [#33](https://github.com/Rawframe-Project/maul-window/issues/33)

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
