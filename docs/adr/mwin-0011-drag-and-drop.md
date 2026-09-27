# mwin-0011. Drag and drop

Status: Accepted

## Context

The requirements (section 9) ask for drops of files and text onto
windows: one bounded record with validated paths, the text, records
while something is dragged over a window so an editor can highlight
where it would land, and no file access granted by the delivery. The
limits (section 11) name the files a drop delivers and their path
bytes. Every platform offers the same shape: the drag enters a window,
moves over it, and leaves or drops, and the dropped data is fetched
from the platform once, sometimes asynchronously (Wayland, X11).

## Decision

- **Records:** `mwin_eventDragEntered`, `mwin_eventDragMoved` and
  `mwin_eventDragLeft` carry the position and what the drag holds
  (`mwin_dragFiles`, `mwin_dragText`); a drop brings
  `mwin_eventDropped` instead of the leaving. The moves are motion,
  merged when motion waits, as the cursor's are; the others are
  discrete input, never merged. Windows take every drag that carries
  files or text; a program that does not want one ignores it.
- **The payload** is copied out with `mwinGetDroppedFiles` and
  `mwinGetDroppedText` under the drop's number from its record, as the
  clipboard's text is: a drop can be larger than a window's text
  storage. The files and text of the last drop stay until the next,
  whose number makes the earlier one stale, so a program that reads a
  late record learns the payload went rather than getting another
  drop's.
- **Paths** come in UTF-8, each ended by a NUL. A path that is not
  UTF-8, or holds a NUL, names no file the program could open, so it is
  left out rather than repaired. Text is repaired as the clipboard's
  (mwin-0010).
- **Limits:** `droppedFiles` (256) bounds the files and `dropBytes`
  (1 MiB) the paths and, apart, the text. What passes them is left out
  whole, never cut, and the record's `truncated` says that anything was
  left out, so a program can tell the user.
- **Access:** a path names a file; nothing is granted with it. On the
  web, where a page never sees paths, files come as their names.
- **Win32:** the backend starts OLE for its thread and registers an
  `IDropTarget` per window, written in C: its function table first in
  a struct kept in the window's own state, so it needs no allocation,
  and its references count nothing, since it lives as long as the
  window. It takes drags whose data object holds `CF_HDROP` or
  `CF_UNICODETEXT` with the copy effect, refuses the rest, and reports
  a drag over only when it has moved, as OLE calls it again and again
  while the drag rests. A drop gathers the paths through
  `DragQueryFileW` and the text as UTF-16 bounded by its memory's
  size. Where OLE cannot start, because the program made the thread
  multithreaded first, the windows accept files instead and a
  `WM_DROPFILES` delivers them, with no drag records.
- **Wayland:** the seat's data device. A drag over one of the
  program's windows whose offer has `text/uri-list` or a text type is
  accepted, files first, with the copy action (data device version 3),
  and reported; a drag of neither, or one over the frame the backend
  draws, is refused with no records. A drop takes the offer on and
  reads each type it has through a pipe of its own a piece at each
  pump, as the clipboard does (the pipe reading is one module for
  both), delivers when both end or after five seconds, marking the drop
  truncated for a read that did not end well, and then finishes the
  offer, so the source knows the copy is done.
- **File URIs:** Wayland and X11 carry files as `text/uri-list`. A
  line that is a `file:` URI with no host or `localhost` gives its
  path, percent-decoded; comment lines are skipped; a URI of another
  scheme or host names no file the program can open, so it is left out
  and the drop marked truncated.
- **Web:** each canvas listens for `dragenter`, `dragover`,
  `dragleave` and `drop`, and takes only drags whose `DataTransfer`
  holds `Files` or `text/plain`, keeping the page's default (opening
  what was dropped) from those alone; other drags pass to the page.
  `dragover` repeats while the pointer rests, so a drag over that has
  not moved is not reported again. A drop's file names and its text,
  encoded with `TextEncoder`, wait in the page until the backend
  gathers them; a name longer than a window's text storage is left
  out, and a drop whose window went is gathered and let go.
- **Memory:** the drop being gathered and the last one delivered each
  hold blocks of their own size from the context's allocator, released
  at the next drop and at the context's end.

## Consequences

The test backend reports the drag's records through `mwinTestPost` and
gathers a drop with `mwinTestDrop`, delivered in order with the other
reports at the next pump; the contract tests cover the records, the
payload, both limits, stale numbers and a drop on a window that went.
Each backend's drag and drop follows in its own change; until then no
backend reports drags.
