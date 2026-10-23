# Development

## Toolchain

GCC 14 or Clang 20, CMake 3.25, Ninja, Python 3, the pinned `clang-format` and `clang-tidy-20`, and for the Linux tests Xvfb, weston and `dbus-daemon`. The organization's CI image (`ghcr.io/rawframe-project/ci-cpp`) has all of them; running the checks in it is the surest way to match CI:

```sh
docker run --rm -v "$PWD:/w" -w /w ghcr.io/rawframe-project/ci-cpp:<version> tools/check.sh pr
```

GCC 13 and older are refused: the library is C23.

ThreadSanitizer turns address randomization off at start where the kernel randomizes more than 28 bits (`/proc/sys/vm/mmap_rnd_bits`), which docker's default seccomp profile forbids: there the TSan preset fails at once inside a container, and runs on the host or with `--security-opt seccomp=unconfined`. GitHub's runners randomize less, so the CI cell is unaffected.

## Building and running tests

Every CI cell is a preset in `CMakePresets.json` with a configure, build and test step of the same name, built in `out/<preset>`:

```sh
cmake --preset linux-clang-20-asan
cmake --build --preset linux-clang-20-asan
ctest --preset linux-clang-20-asan                  # the whole suite, display fixture included
ctest --preset linux-clang-20-asan -L clipboard     # one suite, by its label
ctest --test-dir out/linux-clang-20-asan -LE integration  # what runs without a window system
```

Add a CI cell as a preset, never as commands in a workflow. [Test environments](test-environments.md) covers the presets of the other platforms.

## Generated files

These are committed, because a build must not need Python, and CI regenerates each and fails on any difference:

| File | Generator | From |
|---|---|---|
| `src/generated/pad_linux.c`, `pad_windows.c` | `python3 tools/gen_gamepad_db.py Linux\|Windows <out.c>` | `tools/gamepaddb/` |
| `src/generated/android_keys.c` | `python3 tools/gen_android_keys.py <out.c>` | `tools/androidkeys/` |

The build generates the rest and never commits it: the Wayland protocol code (`wayland-scanner`, with every interface renamed), `maul-window.mjs` for wasm32-wasi builds (`tools/gen_web_glue.py` from the backend's `EM_JS` functions), and the API reference for the documentation site (`tools/gen_api.py`, run by `tools/build_docs.py`).

## Vendored data

`tools/gamepaddb/`, `tools/androidkeys/` and `protocols/` hold third-party files exactly as published, each folder's `README.md` naming the source, the commit and each file's SHA-256. To take a newer SDL_GameControllerDB, replace `gamecontrollerdb.txt` from upstream, update its commit and hash in the README, regenerate both pad tables, and run the gamepad suites; a mapping change is a user-visible change for the pads it touches. The protocols are replaced the same way when a newer `wayland-protocols` release changes them.

## Platform code on Linux

The Windows backend also builds on Linux with a MinGW cross toolchain (a toolchain file for `x86_64-w64-mingw32` of the developer's own), and its tests run under Wine, which helps before a pull request reaches the Windows runner. Wine injects no touch or pen, so those tests skip under it. The Android translator, the X11 pen, touch and scroll modules, the pad mapping, the EDID parser and the key tables have no platform types and run in every build's tests.

## A fuzz finding

Build the fuzz targets with Clang on the test backend:

```sh
cmake -B out/fuzz -G Ninja -DCMAKE_C_COMPILER=clang-20 -DMAUL_WINDOW_FUZZ=ON -DMAUL_WINDOW_TEST_BACKEND=ON -DMAUL_WINDOW_BUILD_TESTS=OFF
cmake --build out/fuzz
./out/fuzz/fuzz_x11_selection -max_total_time=60 out/corpus/fuzz_x11_selection test/seeds/fuzz_x11_selection
./out/fuzz/fuzz_x11_selection crash-<hash>      # reproduce one input
```

Always pass `-max_total_time`. A target's seeds, inputs that reach what random bytes rarely would, are committed in `test/seeds/<target>/`; the runs read them and write what they find to the first folder.

The nightly run keeps each target's corpus from night to night ([Checks](checks.md#fuzzing)). When its summary shows the corpus reaching more than the seeds, download the `corpus-<target>` artifact of that run, copy the target's seeds into an empty folder `kept`, and let `./out/fuzz/<target> -merge=1 kept <artifact folder>` add the inputs that reach what the seeds miss; commit those worth keeping, under a name that says what each reaches.

A crash becomes a test in its module's suite with the minimized input (`-minimize_crash=1`), then the fix.
