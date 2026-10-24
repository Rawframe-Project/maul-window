# Maul Window

Windows, input, gamepads and platform services in C, for games, engines and editors.

[![CI](https://github.com/Rawframe-Project/maul-window/actions/workflows/ci.yml/badge.svg)](https://github.com/Rawframe-Project/maul-window/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/github/license/Rawframe-Project/maul-window)](LICENSE)
[![Release](https://img.shields.io/github/v/release/Rawframe-Project/maul-window)](https://github.com/Rawframe-Project/maul-window/releases)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/Rawframe-Project/maul-window/badge)](https://scorecard.dev/viewer/?uri=github.com/Rawframe-Project/maul-window)

A game or an editor needs the same things from every platform: a window that tells the truth about its size and scale, input that never leaves a key stuck, text from the user's input method, gamepads by where their buttons are, a clipboard, files dropped on it, a file dialog, and the handles to put a GPU surface on. Each platform answers differently, some at once and some much later, some on the program's thread and some only from inside a loop of their own.

Maul Window gives one contract over all of them:

- **Windows** with their modes, styles, owned windows and popups, custom chrome with the platform's own moving, resizing and snap layouts, monitors with their scale, refresh rate and HDR facts, and native handles for a GPU layer.
- **Input:** keys by physical position and by meaning, text and input method compositions, the on-screen keyboard, mouse, touch and pens, system and custom cursors, pointer capture, and which shortcuts the platform keeps.
- **Gamepads** mapped by place through the platform or SDL_GameControllerDB, or raw, with rumble, trigger rumble and motion sensors.
- **Services:** the clipboard (text, data by MIME type, the primary selection), drag and drop, file dialogs, message boxes, opening addresses, revealing files, keeping the display awake, the user's theme, accent, motion, text scale, power and languages, and hooks for accessibility adapters.

Every change to a window is a request answered by exactly one completion, everything arrives in one ordered stream the program drains each frame, no library thread runs and no callback delivers an event. Memory is fixed when the context is created, and everything other programs hand over is validated. A headless test backend lets a program test its window logic on any machine.

## Quick start

It builds with CMake 3.25 or newer and GCC 14 or newer, Clang 19 or newer, or Apple Clang. On Linux and macOS:

```sh
git clone https://github.com/Rawframe-Project/maul-window.git
cd maul-window
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sample_window
```

Ubuntu 24.04's default compiler is GCC 13, which the build refuses ("GCC 14 or newer is required"). Install GCC 14 and configure again in a fresh build directory, since `build/` keeps the compiler it first found; then go on from `cmake --build build`:

```sh
sudo apt install gcc-14
rm -rf build && CC=gcc-14 cmake -B build -DCMAKE_BUILD_TYPE=Release
```

On Windows the library builds with `clang-cl`, and on the web with Emscripten or Clang's wasm32-wasi; [Getting started](docs/user/getting-started.md#requirements) lists what each needs.

Install it with `cmake --install build --prefix <prefix>` and use it from CMake:

```cmake
find_package(maul-window REQUIRED CONFIG)
target_link_libraries(app PRIVATE maul-window::maul-window)
```

or through pkg-config as `maul-window`. On Linux the build needs the Wayland and XCB development files, or `-DMAUL_WINDOW_WAYLAND=OFF` and `-DMAUL_WINDOW_X11=OFF`; [Getting started](docs/user/getting-started.md) lists them, with the build options and the model the API follows.

## Platforms

| Platform | Oldest supported | Built with |
|---|---|---|
| Windows | Windows 10 1703 | `clang-cl` |
| macOS | macOS 11 | Xcode 16 |
| iOS | iOS 15 | Xcode 16 |
| Android | Android 11 (API 30) | NDK r28 |
| Linux | Wayland and X11, chosen at run time | GCC 14 or Clang 19 |
| The web | current browsers | Emscripten, or Clang's wasm32-wasi |

CI builds and tests every platform: the desktops on their runners, iOS in the simulator, Android in the emulator, the web in headless Chrome. The public headers are C17 and C++17, so any C or C++ program can include them; the library itself is C23.

## Documentation

- [The documentation site](https://rawframe-project.github.io/maul-window/), with the API reference generated from the headers.
- [User documentation](docs/README.md): one page per part of the library, and [what each platform asks of a program](docs/user/platforms.md).
- [How it works](docs/internals/overview.md) and [how to develop it](docs/operations/development.md).
- [Changelog](CHANGELOG.md).

Maul Window belongs to the Maul family of standalone C libraries, which share one set of conventions; it depends on [Maul Unicode](https://github.com/Rawframe-Project/maul-unicode).

## Contributing

Issues are welcome. Pull requests are accepted for issues a maintainer has approved; see [CONTRIBUTING.md](https://github.com/Rawframe-Project/.github/blob/main/CONTRIBUTING.md). The project is developed with AI coding agents, directed and reviewed by its maintainers; outside contributions follow the [AI policy](https://github.com/Rawframe-Project/.github/blob/main/AI_POLICY.md).

## License

MIT; see [LICENSE](LICENSE). The gamepad mappings in `tools/gamepaddb/` are under the zlib license, Android's key layout in `tools/androidkeys/` under the Apache License 2.0, and the Wayland protocols in `protocols/` under their own MIT license.
