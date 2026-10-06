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
- **Linux** (Wayland and X11 alike) asks the desktop portal's
  FileChooser (`OpenFile`, `SaveFile`) over the session bus (mwin-0012),
  with the window as the parent on X11 (`x11:` and its id; Wayland needs
  an exported handle, xdg-foreign, which the backend does not make
  yet), `multiple` and `directory`, the filters as globs that match in
  any case (`*.[pP][nN][gG]`, since GTK's globs heed case), the first
  as the current one, the folder as bytes with their NUL, and a save's
  name. The answer is the Response signal on the request, which a
  handle token names before the call, so an answer that comes before
  the call's reply is not missed; a filter on the connection takes it,
  and a match rule sent without waiting asks the bus for it. Its file
  URIs give the paths. A dialog whose request goes, its window
  destroyed or a later dialog, is closed (`Request.Close`).
- **Without a portal** (no bus, or a portal that refuses), zenity, run
  without a shell as the message box is, its output read through a
  pipe without blocking at each pump; several paths come split by a
  control character no file name holds, and output past what the
  limits allow is too large. Without zenity too, dialogs are
  unsupported.
- **Win32:** the common item dialog (`IFileOpenDialog`,
  `IFileSaveDialog`), owned by the window, with file system paths
  only, the filters as `*.png;*.jpg` (Windows matches in any case),
  the first filter's first extension as the default for a save, the
  folder and the name offered. Its `Show` runs a modal loop until the
  user is done. Family record 0017 allows a thread only where a
  platform leaves no other way, and here there is one: the dialog is
  never shown while the program's frame runs (a request waits for the
  next pump, which shows one dialog), and inside its loop a timer on
  the owner runs frames, as it does while a window is moved or sized.
  From that timer a dialog whose request went (a later dialog, or its
  window) or whose program stops is closed through its own window, on
  the same thread. `CarryOut` on Win32 may now answer later, as the
  other backends' do.
- **The web has no dialogs** (`mwin_outcomeUnsupported`): a page's file
  input gives contents, never paths, and reading contents is a matter
  for the program's storage, which the family decides elsewhere.

## Consequences

The test backend answers dialogs with the paths `mwinTestSetDialogFiles`
sets, and describes the last def it was given (`mwinTestGetDialog`);
it may answer cancelled, as a user would. The contract tests cover the
refusals, the copy of the def, the paths and their staleness, each
outcome, and defs given back with their window or at the end under the
sanitizers. The Linux dialogs are tested against a portal of the
test's own on a private bus, which notes what it was asked and answers
with a Response when the test says, and a stand-in zenity. The Win32
dialogs are driven from the test's own frames, which go on while a
dialog shows: they find it among the thread's windows, type a name,
and press OK or Cancel, or end its window or the program.
