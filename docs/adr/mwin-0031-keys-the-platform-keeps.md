# mwin-0031. Keys the platform keeps

Status: Accepted

## Context

The requirements make it a typed platform fact that some keys never
reach the program: a browser's shortcuts first. A rebinding UI needs
it to refuse a binding that can never fire. Each platform keeps
different chords. Chromium dispatches no event for the chords its own
UI owns (Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab), while Firefox and Safari
dispatch them. Windows keeps Ctrl+Alt+Delete, Alt+Tab and Windows+L,
Apple's systems keep Command+Tab, Android keeps its system keys. The
desktops' other shortcuts are the user's to change, and nothing lists
them. Some chords arrive and the platform acts on them too: the web
backend leaves Control and Meta chords to the browser, and Alt+F4
closes a window on Windows. Capturing reserved chords (Keyboard Lock,
Wayland's shortcuts inhibitor, a low-level hook) serves remote
desktops and virtual machines, which the requirements do not ask for.

## Decision

- **A query per chord.** `mwinGetKeyReach` takes a key code and the
  modifiers held (Shift, Control, Alt and Meta; the locks are ignored)
  and answers `mwin_keyReachDelivered`, `mwin_keyReachShared` (its
  records arrive and the platform acts too), `mwin_keyReachUncertain`
  (the desktop's configurable shortcuts may take it) or
  `mwin_keyReachNever`.
- **Tables of rules.** Each platform's answers are a table, a key, a run
  of keys or any key but the modifier keys, with the modifiers needed
  and those allowed besides; the first rule that matches answers, and
  no match is delivered. All tables are compiled on every host, where
  one test checks them.
- **The web** answers from Chromium's table or other browsers' (the
  family from `navigator.userAgentData`), and from the system's under
  it, whichever keeps the chord more.
- **The test backend** answers delivered until `mwinTestSetKeyReach`
  sets a chord, up to 32.
- **No capture** for now; built later, it would change the answers
  while it holds.

## Consequences

A rebinding UI refuses chords that never arrive and warns about shared
and uncertain ones. The answers are the library's knowledge, not a
promise: a desktop may keep chords no table lists, and a browser
release may change Chromium's set, which then needs the table updated.
