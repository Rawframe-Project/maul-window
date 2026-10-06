# mwin-0034. A composition's offsets fit its text

Status: Accepted

## Context

A composition (`mwinPreeditEvent`) carries a caret, a selection and
styled segments as byte offsets into its text. They come from the
platform's input method: Fcitx 5 gives its caret in bytes, Wayland's
text-input-v3 its cursor and anchor in bytes, other backends convert
from characters or UTF-16 units. An input method can hand an offset
inside a character, past the text's end (also after a backend repaired
ill-formed text, which changes its length), or a selection backwards.
The core used to refuse a composition whose offsets lay outside its
text, so the program saw nothing and the composition simply vanished,
and it let an offset inside a character through, so a program slicing
the text at the caret would cut a character in two.

## Decision

- The contract promises that every offset of a composition lies on a
  character boundary within its text, that the selection is in order
  and that no segment is empty.
- The core fits every composition a backend posts before the program
  sees it, in one place for every backend (`preedit_fit.c`): an offset
  past the end becomes the end; the caret moves to the start of the
  character it falls in; a selection or segment reaching into a
  character covers it whole; a backwards selection is put in order; an
  empty segment is dropped. A segment array that cannot be read (too
  many, or missing) is still refused.
- The test backend's reports are fitted the same way.
- A fuzz target checks that whatever comes in, what comes out keeps the
  promise, and that fitting twice changes nothing.

## Consequences

A program can slice a composition's text at any of its offsets and
always gets well-formed UTF-8, and a misbehaving input method costs a
moved caret, not a lost composition.
