# Testing a program

A program's window logic can be tested on any machine, without a window system, on the headless test backend. It exists in builds with `MAUL_WINDOW_TEST_BACKEND` (on with the library's own tests) and only in a context whose def sets `backend = mwin_backendTest`; it is never a fallback for a native backend.

## How it behaves

- It answers each request at the next pump, as a platform's messages arrive. `mwinTestSetAnswer` sets the outcome for a kind of request (denied, unsupported, failed and so on), and `mwinTestHold` keeps every request unanswered until it is released.
- It takes what a platform would report through the functions of `maul-window/test.h`, which go through the same path a real backend's reports take, limits, merging and resets included:
  - `mwinTestPost` reports input and notifications for a window;
  - `mwinTestAddMonitor`, `mwinTestChangeMonitor`, `mwinTestRemoveMonitor` and `mwinTestSetScale` play the displays;
  - `mwinTestAddGamepad`, `mwinTestGamepadButton`, `mwinTestGamepadAxis`, `mwinTestGamepadMotion` and their kin play pads;
  - `mwinTestSetSystemFacts`, `mwinTestSetLocales` and `mwinTestSetKeyReach` set what the system says;
  - `mwinTestSetClipboard` (also in UTF-16, and any bytes), `mwinTestSetClipboardData`, `mwinTestSetPrimary`, `mwinTestDrop` and `mwinTestSetDialogFiles` fill what the user would copy, drop or choose;
  - `mwinTestAskAccessibility` plays an accessibility client asking for a window's tree;
  - `mwinTestSetTime` sets the clock that stamps every record, so time-dependent logic is deterministic.
- It tells what the program asked of the platform: the title (`mwinTestGetTitle`), the clipboard and primary selection written, the addresses opened and files revealed (`mwinTestGetOpened`), the last dialog's def, the icon and cursor shown, the rumble and motion asked of a pad, whether the on-screen keyboard shows and for which purpose, and whether a window accepts text with which caret.

A request the test platform answered with another outcome leaves what these readers tell unchanged, as a platform that refused would.

## A test

A test runs the program's own `init` and `frame` through `mwinRun` with the test backend in the def, posts what it needs between frames, and checks what the program drained and asked for. The library's contract tests in `test/` work this way; `test/test_program.h` is the small harness they share, and `test/test_input.c` and `test/test_clipboard.c` are short examples.
