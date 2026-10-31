# The event stream

The contract promises one ordered stream, memory fixed at creation, and that no discrete record is ever dropped without the program hearing of it. This page is how [queue.c](../../src/queue.c) keeps those three promises together, and the arithmetic behind the limits the context checks.

## Rings and order

Each window has one ring per class of record: notifications, discrete input, motion, raw deltas, the wheel. The context has rings of its own for what belongs to no window (monitors, gamepads, system facts, lifecycle) and two for gamepad buttons and axes. Every record gets a sequence number from one counter, and `mwinContext_NextEvent` hands out the oldest record of all rings. Rings per window mean one busy window cannot push out another's records; the shared counter keeps the order the platform delivered.

Critical records (suspending, resumed, a surface lost or restored) wait in a ring of their own that the stream empties before any other, because the platform is waiting for the program to act on them ([Overview](overview.md#who-runs-the-loop)). Suspending also resets every window's input.

## Notifications coalesce

A notification that reports state (size, pixel size, scale, place, mode, focus, occlusion, visibility, a close request, and the context's monitor changes and facts) replaces one of its class still waiting for the same window, and moves to the end of the stream so order stays true. A program that skipped an intermediate value reads the latest state from `mwinWindow_GetState` at any time.

Because of that replacement, the records that can wait for one window are bounded: one per coalescing class, its created and destroyed records, and one completion per request slot. The context refuses a `notificationsPerWindow` below that bound, so no notification is ever dropped. The same reasoning bounds the context's own ring: a monitor can have its addition, one change and its removal waiting, a gamepad its addition, a change, a reset and its removal; `notificationsPerWindow` must cover three per monitor and four per gamepad besides the context's own classes. A slot (window, monitor, gamepad) is reused only once the record of its end is drained, so a burst of hotplug never outgrows the storage.

## Input: merge or reset

Discrete input is never merged, since a merged key press is a wrong program state. When a discrete record finds its ring full, or its text does not fit the window's text storage, it is lost and `mwin_eventInputStateReset` takes its place in order. A program then knows exactly where its picture of held keys and buttons stopped being true. Every loss of focus posts a reset too, because the platform will not report the releases that happen elsewhere.

Continuous input merges into the newest waiting record of its kind when the ring is full: positions take the newer value, deltas and wheel turns add, and `sampleCount` counts the platform records a record stands for. A touch merges only with its own id. A burst of motion therefore costs no more than the limit and loses no distance.

Gamepad buttons follow the discrete rule (a lost one resets that pad), axes the continuous one per axis.

## Text storage

Text of waiting records (typed text, a composition's text and its segments) lives in one circular byte buffer per window ([text.c](../../src/text.c)). A reservation never wraps: when it does not fit before the end it starts over at the beginning and the skipped bytes count as busy, so every text is one contiguous run the program can read in place. Drained text is reclaimed only when the next pump begins, which is what makes a record's text valid until the frame that drained it returns.

Text is validated before it is stored. Text a user typed or an input method composed is checked as UTF-8; text from other programs (the clipboard, drops) has ill-formed sequences replaced with U+FFFD, as the Unicode Standard recommends, so one bad byte does not lose a paste.

## Compositions are fitted

Input methods hand compositions with offsets in bytes, characters or UTF-16 units, and some hand an offset inside a character, past the end, or a selection backwards. Every composition a backend posts passes through [preedit_fit.c](../../src/preedit_fit.c) before the program sees it: an offset past the end becomes the end, the caret moves to the start of the character it falls in, a selection or segment reaching into a character covers it whole, a backwards selection is ordered, an empty segment is dropped. Fitting is idempotent, which the `fuzz_preedit` target checks. Refusing such compositions, the earlier rule, made them vanish; a moved caret is the better failure.

## Payloads are copied out under their owner

Records stay small. Payloads too large for a record (clipboard text and data, the primary selection, a drop's files and text, a dialog's paths) wait in the context and are copied out with a getter that names what they belong to: the read's request, the drop's number, the dialog's request. Each kind numbers the payloads it finds, and a window keeps per kind its last read answered done and that payload's number, so a getter answers `mwin_errorStale` for any request whose payload a later one replaced. Two windows' reads finishing in one frame can therefore never be answered with each other's bytes. One payload may answer several reads: X11 and Wayland answer every waiting read of a kind with one transfer.
