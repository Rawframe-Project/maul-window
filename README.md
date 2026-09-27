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

Not released. The design is decided and the skeleton builds; the
contract, a headless test backend and the platform backends follow.
Backends are planned for Win32, Wayland, X11 and the web first, then
Android, macOS and iOS.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

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
