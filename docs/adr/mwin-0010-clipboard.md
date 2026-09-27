# mwin-0010. The clipboard

Status: Accepted

## Context

The requirements (sections 9 and 10) ask for UTF-8 text on the
clipboard in the first generation, with images and the X11 primary
selection as typed extensions later. The portable API has to be
asynchronous: the browser's clipboard answers through a promise and
may ask the user first. Wayland lets only a focused client read or set
the selection, and X11 and Wayland make the program that set it serve
it to every reader for as long as it owns it. Clipboard text comes
from other programs, so it is untrusted (section 4) and bounded by a
named limit (section 11).

## Decision

- **Requests:** `mwinRequestClipboardWrite` and
  `mwinRequestClipboardRead` are requests of a window (F19), answered
  by one completion each, superseded by a later request of their kind
  on the same window and cancelled with it, as every request is. The
  window is the one whose focus the platform checks.
- **The text read** is copied out with `mwinGetClipboardText` into the
  program's buffer, not carried in a record. A record's text lives in
  the window's text storage, which is sized for typing, while pasted
  text can be a megabyte; copying out lets the context hold one block
  of the text's own length. The text stays until the next read that is
  done; a refused read or one too large leaves it.
- **Checking:** text the platform gives has each maximal ill-formed
  subpart replaced with U+FFFD, as the Unicode Standard recommends and
  as muni's conversions do, since a paste should not fail over one bad
  byte from another program. UTF-16 (Windows) is converted the same
  way. Text past `clipboardBytes` after that completes the read with
  the new outcome `mwin_outcomeTooLarge`, rather than being cut: a cut
  paste would look complete.
- **The limit:** `clipboardBytes`, 1 MiB by default, bounds both
  directions; a write past it is refused at the call with
  `mwin_errorCapacity`, as is text that is not UTF-8 with
  `mwin_errorInvalid`.
- **Memory:** the written text and the text read each have a block of
  their own length from the context's allocator, taken when a write is
  asked for or a read is done and given back at the next one or at the
  context's end. Nothing is reserved up front for programs that never
  use the clipboard.
- **The written text** stays in the context, where a backend that
  serves the selection (X11, Wayland) reads it for as long as it owns
  it.
- **Win32:** the clipboard is used at once, while the request is
  submitted, as `CF_UNICODETEXT`. Emptying the clipboard first makes
  the window its owner. Another program may hold the clipboard open
  for a moment, so opening it is tried five times a millisecond apart
  before the request fails. Text read is bounded by its memory's size,
  since another program's text need not end with a terminator.
- **Web:** `navigator.clipboard.writeText` and `readText`, the request
  answered when the promise settles: `NotAllowedError` (no focus, or
  the user said no) is `mwin_outcomeDenied`, any other rejection
  `mwin_outcomeFailed`, and a page without the API (outside a secure
  context) `mwin_outcomeUnsupported` at once. The text read is encoded
  with `TextEncoder`, which writes a lone surrogate as U+FFFD, and
  waits in the page until its record is handled; a record carries the
  window's generation, so a read whose window went still takes its
  text and answers no later window in the same slot.

## Consequences

Two reads in one frame on one window paste once; a program that wants
every paste counts the superseded reads. The test backend keeps a
platform clipboard that tests fill with any bytes or UTF-16, answers
reads and writes at the next pump, and lets tests read back what was
written; the contract tests cover repair, both limits, refusals and
cancellation. Each backend's clipboard follows in its own change; until
then its requests complete with `mwin_outcomeUnsupported`. A change
notification (Win32's clipboard listener, Wayland's selection event)
is left for later: the requirements do not ask for one.
