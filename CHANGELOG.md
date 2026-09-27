# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Added

- The library skeleton: the build, the family rules and tools, the
  version and result API (`mwinGetVersion`, `mwinResultName`) and the
  library profile.
- The context and the run function (`mwinRun` with init, frame and
  quit), windows as ids with requests answered by exactly one
  completion (`mwinCreateWindow`, `mwinDestroyWindow`,
  `mwinGetWindowState`, `mwinRequestTitle`, `mwinRequestSize`,
  `mwinRequestPosition`, `mwinRequestMode`, `mwinRequestVisible`,
  `mwinRequestFocus`), and the ordered event stream (`mwinNextEvent`),
  with the named limits (mwin-0002).
- Input: keys by physical code and layout meaning, validated text,
  cursor, buttons, wheel, raw deltas, touch and pen records, in four
  bounded classes per window with merging and input state resets
  (mwin-0004); cursor mode and shape requests (`mwinRequestCursorMode`,
  `mwinRequestCursorShape`); `mwinMapKeyCode` and
  `mwinGetKeyboardLayout`.
- The application lifecycle (suspending, suspended, resuming, resumed)
  and surface loss, delivered before every other record, with a frame
  run at once from inside the platform's call when it waits for the
  program; suspending resets every window's input.
- The headless test backend (`MAUL_WINDOW_TEST_BACKEND`, `test.h`) and
  the contract tests against it.
- Maul Unicode 0.2.0 as a dependency, found installed or fetched
  (mwin-0003).
