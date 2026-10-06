# Maul Window

Windows, input, gamepads and platform services for games, engines and
editors. Written in C23 with public headers any C17 or C++17 program
can include, first-party backends over each platform's own API, and an
MIT license.

It owns what a program needs from the platform's window system:

- windows and their lifecycle, monitors and per-monitor scale, and the
  platform's event loop, whether the program or the platform owns it;
- keyboard, text input and IME, mouse, touch and pen, and gamepads
  with rumble;
- the clipboard, drag and drop, file dialogs and other platform
  services;
- the native handles a GPU layer needs to create a surface, and nothing
  of the GPU itself.

Every change to a window is a request answered by exactly one
completion; events arrive in one ordered stream that the program
drains; no library thread runs, and no callback delivers an event.

[The guide](docs/guide.md) walks through each part, and
[the API reference](docs/api.md) lists all 102 public functions,
generated from the headers.

## Status

0.7.0 is the current release. It has the whole contract on seven
backends, Win32, macOS, iOS, Android, Wayland, X11 and the web (with
Emscripten or as plain wasm32-wasi), and a headless test backend:

1. Windows, their modes and styles, monitors and per-monitor scale, and
   the platform's loop, owned by the program or by the platform.
2. The keyboard, text input and input methods (IBus and Fcitx 5 on
   X11), the keys the platform keeps, the mouse, touch and pen, the
   on-screen keyboard, cursors (the system's shapes or the program's
   images) and pointer capture.
3. Gamepads with rumble, and trigger rumble and motion where the pad
   has them, mapped by SDL_GameControllerDB: Xbox pads through
   Windows.Gaming.Input (XInput before Windows 10) and generic HID pads
   on Windows, GameController on macOS and iOS, Android's input
   devices, evdev on Linux, the Gamepad API on the web.
4. The clipboard (text, data by MIME type, and the primary selection
   on X11 and Wayland), drag and drop, file dialogs, message boxes,
   opening addresses, revealing files and keeping the display awake.
5. Owned windows, popups and custom chrome, and the hooks for
   accessibility adapters.
6. System facts (theme, accent, motion, text scale, power) and the
   preferred locales.
7. A misuse count per context, and every size taken from a count
   checked before it is allocated.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows");
on macOS, Xcode 16's Apple Clang and SDK, the backend running on
macOS 11 or newer (the display link needs macOS 14, a timer driving the
frames before it); for iOS, the same Xcode, the backend running on
iOS 15 or newer, and a program whose Info.plist has a
`UIApplicationSceneManifest` (see the guide, section 11).
`cmake/ios-simulator.cmake` builds for the iOS simulator, where the
tests run on a booted device. For Android, the NDK r28 or newer, the
backend running on Android 11 (API 30) or newer, and an application
that carries the library's Java (`java/maul/window`) and names its
activity, `maul.window.Activity`, in its manifest (see the guide,
section 11). `cmake/android-emulator.cmake` builds for the emulator,
where the tests run through adb.
On Linux the Wayland backend also needs `wayland-scanner` and the
development files of `wayland-client` 1.22, `wayland-protocols` 1.32
and `xkbcommon` 1.0 (`libwayland-dev`, `wayland-protocols` and
`libxkbcommon-dev` on Debian and Ubuntu), or
`-DMAUL_WINDOW_WAYLAND=OFF`; the X11 backend needs the development
files of `xcb`, `xcb-randr`, `xcb-xkb`, `xcb-cursor`, `xcb-render`,
`xcb-xinput` and `xkbcommon-x11` (`libxcb1-dev`, `libxcb-randr0-dev`,
`libxcb-xkb-dev`, `libxcb-cursor-dev`, `libxcb-render0-dev`,
`libxcb-xinput-dev` and `libxkbcommon-x11-dev`), or
`-DMAUL_WINDOW_X11=OFF`, and its tests `xcb-xtest` and `xcb-xfixes`
(`libxcb-xtest0-dev` and `libxcb-xfixes0-dev`). For the web, Emscripten (`emcmake cmake`), or
Clang's wasm32-wasi with wasi-libc
(`-DCMAKE_TOOLCHAIN_FILE=cmake/wasm32-wasi.cmake`), whose build writes
`maul-window.mjs` for the page (see the guide, section 11). The browser
tests run in headless Chrome through puppeteer, found through
`MWIN_NODE_MODULES`, and are skipped without it.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

`samples/window.c` opens a window and prints what the platform says of
it; F11 switches to fullscreen, T accepts text, Escape ends it.

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/mwin.md`.

## License

MIT; see `LICENSE`.
