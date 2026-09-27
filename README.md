# Maul Window

Windows, input, gamepads and platform services for games, engines and
editors. Written in C23 with public headers any C17 or C++17 program
can include, first-party backends over each platform's own API, and an
MIT license.

It will own what a program needs from the platform's window system:

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

## Status

Not released. The contract and a headless test backend are in place;
the Wayland and X11 backends are complete for it, and the Win32
backend has windows, monitors, the keyboard, the mouse, cursors,
touch, pen and input methods. Backends are planned for Win32, Wayland, X11 and the
web first, then Android, macOS and iOS.

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
`-DMAUL_WINDOW_X11=OFF`.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/mwin.md`.

## License

MIT; see `LICENSE`.
