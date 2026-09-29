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
[the API reference](docs/api.md) lists all 77 public functions,
generated from the headers.

## Status

0.1.0 is the current release. It has the whole contract on four
backends, Win32, Wayland, X11 and the web, and a headless test backend:

1. Windows, their modes and styles, monitors and per-monitor scale, and
   the platform's loop, owned by the program or by the platform.
2. The keyboard, text input and input methods, the mouse, touch and
   pen, cursors and pointer capture.
3. Gamepads with rumble, mapped by SDL_GameControllerDB: XInput and
   generic HID pads on Windows, evdev on Linux, the Gamepad API on the
   web.
4. The clipboard, drag and drop, file dialogs, message boxes, opening
   addresses, revealing files and keeping the display awake.
5. Owned windows, popups and custom chrome, and the hooks for
   accessibility adapters.
6. System facts (theme, accent, motion, text scale, power) and the
   preferred locales.

Android, macOS and iOS backends come later; the contract already
builds and passes its tests on macOS.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").
On Linux the Wayland backend also needs `wayland-scanner` and the
development files of `wayland-client` 1.22, `wayland-protocols` 1.32
and `xkbcommon` 1.0 (`libwayland-dev`, `wayland-protocols` and
`libxkbcommon-dev` on Debian and Ubuntu), or
`-DMAUL_WINDOW_WAYLAND=OFF`; the X11 backend needs the development
files of `xcb`, `xcb-randr`, `xcb-xkb`, `xcb-cursor`, `xcb-xinput` and
`xkbcommon-x11` (`libxcb1-dev`, `libxcb-randr0-dev`, `libxcb-xkb-dev`,
`libxcb-cursor-dev`, `libxcb-xinput-dev` and `libxkbcommon-x11-dev`),
or
`-DMAUL_WINDOW_X11=OFF`. For the web, Emscripten (`emcmake cmake`);
its browser tests run in headless Chrome through puppeteer, found
through `MWIN_NODE_MODULES`, and are skipped without it.

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
