# mwin-0001. Library profile

Status: Accepted

## Context

Every Maul library states in one record what its domain adds to the
family rulebook (family record 0005).

## Decision

- **Determinism:** exempt for what the platform delivers (the order and
  timing of input, sizes and scales it chooses). Everything the library
  decides itself is deterministic: the same platform records produce the
  same stream of notifications, which the headless test backend checks.
- **Threads:** none of its own (family record 0017). Platform
  callbacks only publish into the per-window queues. Functions that the
  platform ties to one thread say so with `Main thread only.`.
- **Memory:** the context, created with the caller's allocator, owns
  every window, monitor, gamepad and queue; everything is allocated at
  creation within named limits, nothing on the event path.
- **Platform dependencies:** per backend, the platform's own API and
  system libraries: `user32`, `imm32`, `ole32`, `shell32` and the
  input APIs on Windows; `libwayland-client`, `libxkbcommon` and
  `libudev` for Wayland; XCB and its extensions for X11; the browser
  through Emscripten on the web. Maul Unicode validates platform text.
- **Commit areas:** `api`, `build`, `ci`, `docs`, `events`, `gamepad`,
  `input`, `monitor`, `services`, `tests`, `tools`, `wayland`, `web`,
  `win32`, `window`, `x11`.

## Consequences

A host sees every dependency per backend and knows that no thread runs
the library's code but the ones it calls it from and the platform's
callbacks.
