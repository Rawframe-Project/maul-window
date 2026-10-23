# Test environments

Every backend is tested against the platform itself where a CI runner has it, and against a stand-in of the test's own where it does not. This page says what each environment is and how to run it by hand.

## The display fixture

The X11 and weston tests, and the Linux bus tests that open an X11 window, need a display. CTest starts one for them: the `display_start` fixture runs `tools/test_display.sh`, which starts an Xvfb and, where the Wayland backend is built, a headless weston, private to the build tree; `display_stop` kills the PIDs it recorded and nothing else. The X display number comes from a hash of the build directory and weston's socket is `wayland-mwin` in `out/<preset>/rt`, which the tests get as `DISPLAY`, `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR`. A developer's own display is never touched, and two build trees never share a server. The Xvfb runs with `-noreset`: by default it resets whenever its last client disconnects, and a test connecting during the reset would fail. The fixture is ready when Xvfb says it takes clients (`-displayfd`), not when its socket file appears, and a repeated start (`ctest --repeat`) keeps the servers it started. Since the server keeps its state from test to test, a test that changes it (output properties, RandR modes) puts it back with checked requests before it disconnects: the server drops requests still unanswered when a client goes. The X11 tests skip only where `DISPLAY` is unset; with `DISPLAY` set, a test that cannot reach the server fails with xcb's error (`test/x11_display.h`), so a broken fixture never passes as a skip.

A start that fails prints the servers' logs; they stay in `out/<preset>/rt/` (`xvfb.log`, `weston.log`).

## Stand-ins

- **Wayland:** most Wayland tests run a small compositor of their own on `libwayland-server` (`test/wayland_server.h` and its protocol extensions), since a headless weston has no seat, tablet, text input or activation to drive. It records what the backend asked for: parents and positioners, grabs, idle inhibitors, icon buffers, decoration modes.
- **D-Bus:** `test/linux_bus_fake.h` starts a private `dbus-daemon` and serves stand-ins for the portal (settings, file chooser, inhibit, power profiles), the file manager, the screensaver, UPower on a private system bus, and IBus and Fcitx 5. Nothing on the machine's own buses is touched.
- **Programs:** stand-in `xdg-open`, `zenity` and `kdialog` scripts on `PATH`.
- **Windows:** XInput, Raw Input with the HID parser, and Windows.Gaming.Input are tables of functions the tests replace, since no virtual pad of those kinds can be made; the real runtime is still started and stopped where the system has it.
- **Gamepads on Linux:** real kernel devices made through `/dev/uinput` (the `uinput` label). Locally that needs write access to `/dev/uinput` and read access to `/dev/input/event*`.

## Windows

The Windows tests run on the runner's interactive desktop, driving it through `SendInput`, touch injection and a synthetic pen, and some change the user's settings for a moment and put them back. They run one at a time: any test's window can take the foreground another test's input needs. They also run under Wine for a MinGW build, which injects neither touch nor pen; those tests skip there. Run them with `cmake --preset windows-clang-cl`, `cmake --build --preset windows-clang-cl` and `ctest --preset windows-clang-cl` on a desktop session.

## macOS and iOS

The macOS tests need a logged-in session's window server, which CI's macOS runners have; they make the `NSEvent`s AppKit would deliver, since real input cannot be made without permissions. `cmake --preset macos-clang`.

The iOS tests run in a booted simulator: `cmake/ios-simulator.cmake` builds for it, headless tests run through `simctl spawn`, and UIKit tests are applications run by `tools/run_ios_app.sh`, which installs and launches one, reads its output from a file named in its environment, waits at most a minute for its closing line, and shows where a stuck application is (`sample`) or its crash report. Boot a simulator, set `MWIN_IOS_DEVICE` to its UDID, then use the `ios-simulator` presets. A new device's first launch takes minutes; CI pays it once before the tests.

## Android

`cmake/android-emulator.cmake` wraps the NDK's toolchain file. Executables run through `tools/run_android.sh` (`adb push`, `adb shell`); window tests are applications built without Gradle by `tools/build_android_app.sh` (javac, R8, aapt2, zipalign, apksigner) and run by `tools/run_android_app.sh`, which installs and starts one, carries out the system actions the test asks for (the home key, a rotation) and waits for its result line. The runner turns the system's animations off while a test runs and puts back the night mode, font scale, battery saver, battery and accessibility services after it. With an emulator booted, `ANDROID_HOME` and `ANDROID_NDK_HOME` set and `ANDROID_SERIAL` naming it, use the `android-emulator` presets. Stop the emulator when the run ends.

## The web

The web tests run in headless Chrome through puppeteer (`test/web_runner.cjs`), found through `MWIN_NODE_MODULES`. The runner serves the build, gives each test a page and a canvas, and carries out what the test asks: a device pixel ratio or color scheme, keys, the mouse and wheel through puppeteer, touches, a pen and compositions through the DevTools protocol, a hidden page and the back-forward cache. A wasm32-wasi test is a reactor whose `main` the runner calls, with a minimal WASI and the generated imports.

`test/browser/` pins puppeteer and its dependencies in a lockfile:

```sh
mkdir -p out/browser && cp test/browser/package*.json out/browser/ && npm ci --prefix out/browser
MWIN_NODE_MODULES=$PWD/out/browser/node_modules ctest --preset web-emscripten
```
