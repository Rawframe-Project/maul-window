# mwin-0013. File dialogs

Status: Accepted

## Context

The requirements (section 9) ask for file dialogs: open one file or
many, save one, and choose a folder, with filters, as asynchronous
requests with typed results, through xdg-desktop-portal on Linux. The
platforms differ in how they run: the portal answers over the session
bus whenever the user is done; Windows' common item dialog runs a modal
loop of its own until it closes; a page offers a file input that yields
contents without paths.

## Decision

- **A request of a window:** `mwinRequestFileDialog` with a def (kind,
  title, starting folder, the name a save dialog offers, filters),
  answered by a completion like any other; frames go on while the
  dialog shows. The window is the dialog's parent, which every
  platform needs to place it and keep it above.
- **The def is copied at the call** into one block from the context's
  allocator that the request holds and gives back when it is answered,
  or at the context's end: the platform may read it a frame or a minute
  later.
- **Filters are extensions**, without dots, separated by `;`
  (`"png;jpg"`), matched in any case. Every platform takes them: glob
  patterns for the portal, `*.png;*.jpg` for Windows, `.png,.jpg` for
  an input's `accept`. Arbitrary globs or MIME types would not mean the
  same everywhere. Folder dialogs ignore filters; no filters offers
  every file.
- **Outcomes:** `mwin_outcomeDone` with the paths chosen;
  `mwin_outcomeCancelled` when the user closes the dialog, which the
  outcome's meaning now includes beside a window destroyed first;
  `mwin_outcomeTooLarge` past the new `dialogFiles` (256) and
  `dialogBytes` (1 MiB) limits, rather than a choice cut short; failed
  for a platform that answered done with no path, or one that is not
  UTF-8 and so names no file the program could open.
- **The paths** are copied out with `mwinGetDialogFiles` under the
  request's id, as drops are under their number: absolute, UTF-8, each
  ended by a NUL, with their count. They stay until the next dialog
  done, which makes the id stale; a dialog cancelled, too large or
  failed leaves them. The growing list of paths is one module for
  drops and dialogs.
- **The web has no dialogs** (`mwin_outcomeUnsupported`): a page's file
  input gives contents, never paths, and reading contents is a matter
  for the program's storage, which the family decides elsewhere.

## Consequences

The test backend answers dialogs with the paths `mwinTestSetDialogFiles`
sets, and describes the last def it was given (`mwinTestGetDialog`);
it may answer cancelled, as a user would. The contract tests cover the
refusals, the copy of the def, the paths and their staleness, each
outcome, and defs given back with their window or at the end under the
sanitizers. Each backend's dialogs follow in their own change.
