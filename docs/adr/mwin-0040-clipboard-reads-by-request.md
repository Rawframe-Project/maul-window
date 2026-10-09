# mwin-0040. A clipboard read's payload is kept under its request

Status: Accepted

## Context

A clipboard read that completes done left what it found (text, data of
a type, or the primary selection's text) in one place in the context,
and `mwinGetClipboardText`, `mwinGetClipboardData` and
`mwinGetPrimaryText` copied out whatever that place held (mwin-0010,
mwin-0029). Two windows' reads that both complete before the program
drains its queue leave the second's payload, which the program then
took as the first read's answer. The backends also store the bytes a
read finds before its completion is posted, so a read that a later one
superseded could still replace what a completed read left. Nothing told
the program. The file dialog already keeps its paths under the request
that found them and answers `mwin_errorStale` for any other.

## Decision

- The three getters take the read's request id after the context. Each
  kind of read (text, data, primary text) numbers the payloads it
  finds; a window keeps, per kind, its last read answered done and the
  number of the payload that answered it. A getter copies out only for
  a request that is such a read and whose payload is still the last of
  its kind, and answers `mwin_errorStale` otherwise: a null request, a
  request of another kind, a read refused or too large, or one whose
  payload a later read replaced.
- One payload may answer several reads: X11 and Wayland answer every
  waiting read of a kind with one transfer.
- No allocation: the numbers live in the context and the window slots.
- `mwinRequestClipboardWriteData` takes its item count as `uint32_t`,
  as the other requests with arrays do.

## Consequences

A completion is never answered with another read's payload; the
mistake is a returned status. Programs pass the completion record's
request to the getter, a source change made before 1.0 and written in
the changelog. A read refused or too large still leaves the earlier
read's payload readable under that earlier read.
