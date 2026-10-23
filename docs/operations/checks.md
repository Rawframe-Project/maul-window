# Checks

What verifies a change, locally and in CI, and the time each may take.

## Locally

`tools/check.sh` runs the Linux checks in three tiers; each includes the one before it. Its builds are the CI cells' presets from `CMakePresets.json`, in `out/<preset>`, and the other trees go to `out/check-*`, each with its log beside it.

| Tier | Runs | Budget |
|---|---|---|
| `changed` | The source, module, length, API documentation and snippet rules, the organization's documentation check, `clang-format` on the changed C and Objective-C files, and in the `linux-clang-20-asan` preset the tests whose labels the changed files reach, integration tests left out. | under 2 minutes |
| `pr` | Every rule on every file, the generated tables against their generators, that the documentation site builds, clang-tidy over the Linux build's sources, the whole `linux-clang-20-asan` suite with the display fixture, and 10 seconds of fuzzing per target. | under 10 minutes |
| `full` | Also the other Linux presets (ThreadSanitizer, shared, minimal, Clang and GCC Release), the virtual gamepad tests where `/dev/uinput` can be written, and 60 seconds per fuzz target. | under 20 minutes |

Run `changed` before every push. On the shared development machine, run `pr` and `full` through the workspace's heavy-job wrapper. The macOS, iOS, Android, Windows and web cells run in CI; [Test environments](test-environments.md) says how to run each by hand.

## Tests and labels

Each suite is `test/test_<name>.c` (or `.m`), one executable registered with CTest as `<name>` and labeled `<name>`, so `ctest --preset <preset> -L <name>` runs one. Other labels:

- `integration`: tests that drive a real window system, server, device, simulator, emulator or browser: the X11 and weston tests against the display fixture, the Wayland tests against a stand-in compositor on a thread of their own, the Linux bus tests, the virtual gamepad tests, and every macOS, iOS, Android, Windows desktop and web test. They may read the clock, take about a second each, and stay out of the `changed` tier.
- `quarantine`: a test that failed intermittently, kept out of the required check and run every night until the issue named in the comment above its label is fixed. It is never retried.
- `uinput`: the tests that make virtual gamepads through `/dev/uinput`. A container job has no input devices, so the Linux test presets leave them out; the `full` tier runs them where the device can be written, and CI every night.

The Linux tests that need a display get the build's own Xvfb and headless weston from the display fixture ([Test environments](test-environments.md#the-display-fixture)); the stand-in compositors and buses need nothing installed but what the image has. Without integration tests the `changed` tier runs the contract on the test backend and the modules that take plain values, which cover most of the core. A test that cannot run exits 77, which CTest reports as skipped, and every CI cell fails on a skipped or unrun test, so a platform that did not start never passes with nothing tested. A failed test job uploads its CTest logs, kept for a week.

## In CI

`.github/workflows/ci.yml` runs on every pull request, every push to `main` and every night. The Linux jobs run in the organization's CI image, pinned by digest, and install nothing. The ruleset requires only its `required` job, which passes when every other job passed or was skipped, and the title check.

| Job | What it guards |
|---|---|
| `changes` | Whether a pull request changes anything beyond documentation; when it does not, the build jobs are skipped. |
| `docs` | The organization's documentation check. `protocols/`, `tools/gamepaddb/` and `tools/androidkeys/` hold third-party files and are excluded. |
| `zizmor` | The workflows: injection, persisted credentials, permissions, unpinned actions. |
| `checks` | `clang-format`, the family's rules, that the documentation site builds, that the gamepad and Android key tables match their generators, and clang-tidy. It runs on documentation changes too. |
| `build` | One call of the organization's `cpp.yml` per preset: GCC 14 and Clang 20 in Release, Clang with AddressSanitizer and UBSan (each test repeated up to three times on a pull request, so a flaky one fails before it merges), Clang with ThreadSanitizer, the shared library, the test backend alone, macOS on its runner and `clang-cl` on Windows. All build with warnings as errors. |
| `msvc-headers` | Every public header compiled by MSVC's `cl` as C17 and C++17 with `/W4 /WX`. |
| `fuzz` | Every fuzz target from its committed seeds, as many at a time as the runner has cores: 20 seconds each on a pull request, 90 on `main`. |
| `fuzz-nightly` | Every night, each fuzz target in a job of its own for 600 seconds, from its corpus of the nights before ([Fuzzing](#fuzzing)). Not part of the required check. |
| `ios-simulator` | The iOS backend and its tests built on every pull request; on `main` and at night, run in a booted simulator of the newest runtime. A simulator's boot and first launch take longer than the pull request budget leaves. |
| `android-emulator` | The Android backend in an Android 11 emulator on the Linux runner with KVM. |
| `web-wasi` | The web backend built by Clang for wasm32-wasi in the CI image, every test a module in headless Chrome with the generated JavaScript imports. |
| `web-emscripten` | The web backend built by Emscripten, its tests in headless Chrome through puppeteer, and what the backend adds to a program's size. |
| `gamepads` | Every night: the virtual gamepad tests, in the CI image run by `docker` with the runner's `/dev/uinput` passed in. A skipped test fails it. |

`pr-title.yml` checks that the pull request title is a conventional commit with a scope from `AGENTS.md`, and `pr-gate.yml` closes a pull request from outside the organization unless it closes an issue labeled `accepted`. `codeql.yml` runs CodeQL in the CI image on every pull request and on `main`. `scorecard.yml` grades the repository's security practices weekly, and Dependabot proposes updates of the pinned actions once a week.

## Fuzzing

The fuzz targets (`test/fuzz_<area>.c`, built with `-DMAUL_WINDOW_FUZZ=ON` and Clang on the test backend) cover the bytes other programs and devices hand the library: EDID blocks, payloads (`text/uri-list`, dropped and pasted text in UTF-8 and UTF-16, zenity's output), compositions, POSIX locale names, X11 scroll valuators, gamepad reports through their mapping, and the X11 clipboard and primary selection reads (`fuzz_x11_selection`: the owner's answer, the property whole or in INCR pieces, and the events other clients cause meanwhile, against a stand-in X server). Each checks properties that hold for any input: no crash, text kept within its limit and valid UTF-8, a composition's offsets on character boundaries and fitting idempotent, sticks and triggers within their ranges, every clipboard request answered exactly once. A crash a fuzzer finds becomes a test in its module's suite ([Development](development.md#a-fuzz-finding)).

Each target starts from its committed seeds (`test/seeds/<target>/`), inputs that reach what random bytes rarely would. The nightly run also carries a corpus per target from night to night in an Actions cache:

- The cache key names the target and hashes the fuzz build's inputs (`src/`, `include/`, `protocols/`, `cmake/`, `CMakeLists.txt`, the target's source) and the compiler's version, so an unchanged build restores its own corpus exactly. A target whose build changed restores its latest corpus of an earlier build: a corpus is only inputs, which stay valid against a new binary.
- After its 600 seconds, the fuzzer's merge step re-scores the corpus over the seeds and keeps only what adds coverage, and that is saved for the next night.
- The run's summary says which corpus was restored (this build's, an earlier build's, or none) and gives the seeds' coverage beside the corpus's, and the corpus is uploaded as `corpus-<target>` for seven days. Where the corpus reaches more, the inputs worth keeping are committed as seeds ([Development](development.md#a-fuzz-finding)).

## Budgets

Over a budget, tests are pruned or merged before anything else is added. The pull request check's longest job is the Android emulator (about five minutes); the whole Linux suite runs in a few seconds once built. The iOS simulator run (eight to eleven minutes, most of it booting) is on `main` and at night.
