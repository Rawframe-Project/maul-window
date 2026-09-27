# mwin-0004. Input records, their storage and their limits

Status: Accepted

## Context

Input must never wedge a held key, discrete records may never vanish
silently, high-rate motion must stay available in order or as a bounded
batch, and text from the platform is untrusted. Rebinding screens need
the physical key and the character it types as separate facts.

## Decision

- **Keys** carry a code and a key. The code is the USB HID keyboard
  usage, named as the W3C `KeyboardEvent.code` values name it, the same
  on every layout. The key is what the current layout makes of it: the
  code point it types unmodified, or `MWIN_KEY_NAMED` with the code for
  a key that types nothing. `mwinMapKeyCode` and `mwinGetKeyboardLayout`
  answer from the platform; no layout data ships (W5).
- **Classes.** Each window stores input in four rings with the limit
  `inputPerWindow` each: discrete (keys, text, buttons, touches and pen
  contacts beginning or ending), motion (cursor, touch and pen), raw
  deltas, and the wheel. The stream orders all of them with the
  notifications by one sequence.
- **Full storage.** A discrete record that finds its ring full is lost,
  and `mwin_eventInputStateReset` takes its place in order; so does a
  text that does not fit in `textBytesPerWindow`. A continuous record
  merges into the newest waiting record of its kind (a touch only with
  its own id): positions take the newer value, deltas and wheel turns
  add, and `samples` counts what the record stands for. Every loss of
  focus is followed by a reset as well.
- **Text** is validated UTF-8, copied into the window's text storage,
  and valid until the frame that drained it returns: drained text is
  reclaimed only when the next pump begins.
- **Input methods.** A composition is `mwin_eventImePreedit`, a
  discrete record whose newer value replaces one still waiting; its
  segments are copied into the text storage before its text. A commit
  is `mwin_eventTextInput`. While a window composes, a key record whose
  key types a character and goes down is left out, and so is the
  release of that key, so no press or release arrives alone; keys that
  type nothing pass.
- **The test backend** delivers what a test reports at the next pump,
  as a platform's messages arrive.

## Consequences

A program learns of every loss of input as a reset at the point it
happened, and never sees held state it cannot trust. Memory for input
is fixed at creation, and a burst of motion costs no more than its
limit.
