# mwin-0002. The window contract

Status: Accepted

## Context

The requirements ask for windows named by generation-checked ids, every
change as an asynchronous request with a typed answer, a two-phase
close, and bounded queues that never drop a discrete record silently.
Family records 0016 to 0018 give the shapes; this record fixes what
they leave open for windows.

## Decision

- **Creation** returns the window's id at once, with the id of a
  creation request. The platform makes the window later:
  `mwin_eventWindowCreated`, the notifications of its first size, scale
  and mode, then the request's completion. A window whose creation
  fails stays an id the program destroys.
- **Destruction** is immediate, not a request: the id is stale at once,
  its requests in flight complete as cancelled, its notifications still
  waiting are dropped, and `mwin_eventWindowDestroyed` follows. The
  slot is reused only after that record is drained.
- **Requests** of one kind on one window supersede each other: the
  older completes as superseded at once. A request slot is free again
  only when its completion is drained, so completions waiting never
  outnumber the slots. A completion follows the notifications its
  change caused.
- **Notifications** that report state coalesce: a newer one of the same
  class (size, pixel size, scale, position, mode, focus, occlusion,
  visibility, close request) replaces one still waiting for the same
  window and moves to the end of the stream. With the created and
  destroyed records and the completions, a window can have at most
  `requestsPerWindow + 11` records waiting, and the context refuses a
  notification limit below that, so no notification is ever dropped.
- **Mode** is one notification, `mwin_eventModeChanged` with the new
  mode, in place of SPEC-0025's separate `minimized`, `maximized` and
  `restored`, which are that notification with `data.mode`.
  `mwin_eventShown` and `mwin_eventHidden` answer visibility requests.

- **Critical records.** The lifecycle and surface records wait in a
  ring of their own that the stream empties first. When a platform
  waits for the program before it goes on (Android's pause, a lost
  surface), the backend runs a frame at once from inside the platform's
  call, so the program handles the record in time; a frame that stops
  there ends the loop. Suspending resets every window's input.

- **Monitors** are ids in slots like windows': a disconnected
  monitor's slot waits until its removal is drained, so a monitor
  connected again gets a new id. Changes of one monitor's facts merge.
  Each monitor can have at most its addition, one change and its
  removal waiting, and the context requires room for three per monitor.

## Consequences

Every request is answered exactly once, including those of a window
destroyed first, and memory for records is fixed at creation. A
program reads the latest state from `mwinGetWindowState` whenever the
coalesced stream skipped an intermediate value.
