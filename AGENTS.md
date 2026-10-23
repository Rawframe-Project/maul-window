# Maul Window

Maul Window is a C library that gives games, engines and editors their windows, input, gamepads and platform services on Windows, macOS, iOS, Android, Wayland, X11 and the web, and the native handles a GPU layer needs. It is part of the Maul family; the family rules are in [docs/internals/maul-conventions.md](docs/internals/maul-conventions.md), and [docs/internals/overview.md](docs/internals/overview.md) explains how the core and the backends fit.

It never compromises on:

1. **One contract on every platform.** Every change to a window is a request answered by exactly one completion; everything arrives in one ordered event stream; the library starts no thread and calls the program only through init, frame and quit. A platform that cannot do something answers unsupported; it never behaves differently in silence.
2. **Nothing lost silently.** Discrete input is never merged or dropped without a reset in its place; no notification is ever dropped; a payload is never answered for the wrong request.
3. **Fixed memory.** Everything is allocated at context creation within the named limits of `mwinLimits`; nothing is allocated on the event path.
4. **Hostile input.** Everything a platform, another program or a device hands over is validated or repaired before the program sees it, in every build.
5. **No graphics.** The library hands out native handles and includes no graphics API header.

## Commands

```sh
tools/check.sh changed          # before every push: rules, format, the tests the changes reach (< 2 min)
tools/check.sh pr               # what the pull request check runs on Linux (< 10 min)
tools/check.sh full             # adds TSan, shared, minimal, Release builds, uinput pads, longer fuzzing (< 20 min)
cmake --preset linux-clang-20-asan && cmake --build --preset linux-clang-20-asan   # one CI cell
ctest --preset linux-clang-20-asan -L clipboard     # one suite, by label
ctest --test-dir out/linux-clang-20-asan -LE integration   # what needs no window system
python3 /home/rawframe-project/.github/maul/family_sync.py --check maul-window   # shared files
```

Run the `pr` and `full` tiers through the workspace's heavy-job wrapper on the shared machine. Use gcc-14 or clang-20; the machine's default gcc is too old for C23. Every CI cell is a preset in `CMakePresets.json` (configure, build and test of one name, built in `out/<preset>`); add a cell there, never as commands in a workflow. The macOS, iOS, Android, Windows and web cells cannot run on Linux: [docs/operations/test-environments.md](docs/operations/test-environments.md) says how they run.

## Ways to hurt yourself

- **Killing by pattern.** Never `pkill -f`, `pgrep | kill` or `kill` a PID found by matching a name or path: other sessions build, test and run Xvfb, weston, emulators and fuzzers on the same machine. Kill only a PID you started. The display fixture records its own PIDs and stops only those.
- **Touching a real display or bus.** Tests reach the build's own Xvfb and weston through the display fixture and a private `dbus-daemon` of their own. Never point a test at a developer's `DISPLAY`, `WAYLAND_DISPLAY` or session bus, and never run the Windows tests on a desktop someone uses: they move the focus, inject input and change user settings for a moment.
- **Leaving emulators and simulators running.** Stop the Android emulator or iOS simulator you booted when the run that needed it ends.
- **Endless fuzzing.** A libFuzzer target runs until it finds something. Always pass `-max_total_time`.
- **Editing generated or vendored files.** `src/generated/` is written by `tools/gen_gamepad_db.py` and `tools/gen_android_keys.py`; `tools/gamepaddb/`, `tools/androidkeys/` and `protocols/` are kept exactly as published, with their hashes in their READMEs. Change the generator, or replace the data as [development.md](docs/operations/development.md#vendored-data) describes.
- **Editing shared family files.** These are synced from the canonical copies in the organization's `.github` repository (`maul/`) and fail CI when they differ: `docs/internals/maul-conventions.md`; `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`; `cmake/CompilerFlags.cmake`, `cmake/Install.cmake`, `cmake/Library.cmake`, `cmake/ReadVersion.cmake`, `cmake/Testing.cmake`, `cmake/Config.cmake.in`, `cmake/pkg-config.pc.in`, `cmake/HeaderCheck/CMakeLists.txt`; `bench/CMakeLists.txt`; `test/test_harness.h`; and `tools/build_docs.py`, `check_docs.py`, `check_guide.py`, `check_lengths.py`, `check_modules.py`, `check_source.py`, `gen_api.py`. Change the canonical copy first, through the manager.
- **Pushing a Java member reached by name without a keep rule.** The native library finds `java/maul/window` classes and members through JNI. A new one needs a rule in `java/proguard-rules.pro`; the Android tests are built by R8 and fail without it.

## Invariants

- **Core and backends.** The core ([src/core.h](src/core.h), [src/backend.h](src/backend.h)) never calls a platform API; a backend reports through the core's functions and answers every request it is given through `mwinComplete`, now or later. A request answered later carries its slot and generation ([src/answer.h](src/answer.h)), never a pointer.
- **Modules:** `tools/modules.txt` lists which module may include which. A new include needs a line there.
- **No platform types in shared logic.** Code that tests should run on every platform (the Android motion translator, X11 pen, touch and scroll, pad mapping, EDID, key tables, preedit fitting) takes plain values, never a platform handle.
- **Linux libraries are opened at run time**, per context, into tables; never call `libwayland-client`, `libxcb`, `libxkbcommon` or `libdbus-1` by name. The Wayland protocol code is generated with an `mwin_` prefix and its inline wrappers are poisoned.
- **Web JavaScript** exists once, as `EM_JS` functions in the C files; `tools/gen_web_glue.py` makes the wasm32-wasi imports from them. Every JavaScript function name starts with `mwin`.
- **Library version:** `MWIN_VERSION_*` between the release-please markers in `include/maul-window/base.h`; CMake reads it there and `MWIN_ABI_VERSION` follows from it. Only release-please changes it. The application def is the root def and the only one that carries the ABI version.
- **Public headers** are the common subset of C17 and C++17 and declare only the API; every public function has a `///` block with `@param`, `@return` and a thread safety paragraph from the list in `tools/check_docs.py`.
- **Assertions:** internal invariants only, on in Debug, sanitizer and fuzz builds. Never assert on an argument, a platform report or other programs' bytes: validate and return a status or repair the text.
- **Limits:** every bound on memory or work is a field of `mwinLimits` or a named `MWIN_*` constant in a public header; a new bound is one of them, with a default.
- **Workflows:** every action is pinned by commit SHA with its version in a comment, every job declares the least `permissions` it needs, and no job installs packages the CI image could carry.

## Testing

Follow the [repository standard's Tests section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#tests); in this repository:

- Test the contract on the test backend through the public headers (`test/test_program.h`); test a backend against the platform's own reading of what it did (the X server's properties, Windows' styles, the stand-in compositor's records), never against the backend's private state.
- A test that drives a real window system, device, simulator, emulator or browser carries the `integration` label (`mwin_display_tests` or `mwin_integration_tests` in `CMakeLists.txt`); keep them to end-to-end scenarios.
- A test that cannot run exits 77; CI fails on any skipped test, so a skip is only for a platform that is genuinely missing locally.
- Where a platform cannot be driven (XInput, Raw Input, Windows.Gaming.Input, GameController's pads), test against a stand-in table of functions, and say in the test what is not exercised.
- Every C snippet of `docs/user/` appears unchanged in `test/test_guide*.c`, or carries a `<!-- guide: not run: REASON -->` comment; `tools/check_guide.py` enforces it.
- A fuzz finding becomes a test in its module's suite with the minimized input.

## Documentation

Follow the [repository standard's Documentation section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#documentation). A change in what a caller must do or can rely on updates the page of `docs/user/` for that part; a change in how a backend works or why updates its `docs/internals/` page. The API reference is generated from the headers into the documentation site; never commit it.

## Pull requests

Titles are conventional commits. Scopes: `core`, `window`, `monitor`, `input`, `text`, `gamepad`, `clipboard`, `drop`, `services`, `system`, `access`, `test-backend`, `win32`, `macos`, `ios`, `android`, `wayland`, `x11`, `linux`, `web`, `generator`, `build`, `bench`, `family`. Example: `fix(x11): keep a touch's pressure when an update leaves it out`.

A change to `src/generated/` names the data or generator change that caused it.
