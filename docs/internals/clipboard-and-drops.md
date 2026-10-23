# Clipboard and drops

## Why requests and getters

The browser's clipboard answers through a promise and may ask the user; Wayland lets only a focused client use the selection; iOS asks before reading another application's text. So every clipboard operation is a request of a window, the one whose focus the platform checks. Pasted text can be a megabyte while a window's text storage is sized for typing, so the payload is copied out with a getter into the program's buffer rather than carried in a record ([The event stream](events.md#payloads-are-copied-out-under-their-owner)).

Text past `clipboardBytes` completes too large rather than cut, because a cut paste looks complete. The written text and the text read each take a block of their own length from the context's allocator, so a program that never uses the clipboard reserves nothing.

## Data by MIME type

Every platform's clipboard holds data by type underneath (a registered format, a target atom, a MIME type, a uniform type). The library passes bytes through and converts nothing: converting images between formats needs a codec a window library has no other use for. Win32 registers a format per MIME type (`PNG` for `image/png`), Apple's systems map the type to a uniform type, the web writes a `ClipboardItem` (types beyond the three the standard mandates prefixed `web `), Android answers data requests unsupported. A Windows program that reads only `CF_DIB` sees no image from a program that writes PNG; that conversion stays the program's.

## Serving the selection on X11 and Wayland

On both, the program that set the selection serves it to every reader for as long as it owns it, through its own loop. So the written text and data stay in the context, and while the program owns the selection a read answers from its own copy rather than asking itself through the same loop. A read while one is under way joins it, and one transfer answers every waiting read of its kind ([selection_reads.c](../../src/selection_reads.c)).

- **Wayland:** a data source offering the text as `text/plain;charset=utf-8`, `UTF8_STRING` and `text/plain`, quoting the serial of the latest input. Each reader's pipe is written a piece at each pump without blocking, up to four readers at once, so a slow or huge reader never stalls the program. A reader that went would raise SIGPIPE, which ends a process by default and which a library may not ignore process-wide, so the signal is blocked for the writing thread and a pending one taken back. A read takes the best text type through a non-blocking pipe, grows its buffer to one byte past the limit, and fails after five seconds without an end ([wayland_pipe.c](../../src/wayland_pipe.c)).
- **X11:** the `CLIPBOARD` and `PRIMARY` selections, owned by a hidden input-only window made at first use, so the selection outlives the program's windows. Ownership quotes the time of the latest key or button event, as ICCCM asks. The owner answers `TARGETS`, `TIMESTAMP` and the UTF-8 types, sends text past 64 KiB in pieces (INCR) to up to four readers, each dropped after five seconds without taking one. A read asks for one word past the room left, so text too large is known without taking it all.

## Drops

A drop is gathered once, sometimes asynchronously (Wayland and X11 read the offer through pipes or properties), and delivered as one record with its payload under the drop's number. The last drop's payload stays until the next one makes the number stale, so a program reading a late record learns the payload went instead of getting another drop's.

Wayland and X11 carry files as `text/uri-list`: a `file:` URI with no host or `localhost` gives its percent-decoded path; any other scheme or host names no file the program can open, so it is left out and the drop marked truncated ([file_list.c](../../src/file_list.c) gathers the paths for drops and dialogs alike). A path that is not UTF-8 is left out rather than repaired, since a repaired path names another file.

- **Win32:** an `IDropTarget` per window written in C, its function table first in a struct kept in the window's own state, so it needs no allocation and its reference count means nothing. OLE calls it again and again while a drag rests, so a drag over is reported only when it moved. Where OLE cannot start (the program made the thread multithreaded first), windows accept files through `WM_DROPFILES`, without drag records.
- **X11:** XDND 5. The enter message carries no position, so the drag is reported entered at its first position. Text sent in pieces is left out and the drop marked truncated.
- **iOS and Android:** a dropped document is handed over only while its load runs, and a descriptor of a content address cannot be opened again by path on Android, so each document is copied into the application's temporary or cache folder, a folder per drop, and the drop is posted once every copy is whole. Android copies at most 8 MiB a frame, from a descriptor that never blocks, so a large document does not stall the frames.
- **The web:** a page sees file names, never paths. Only drags holding `Files` or `text/plain` are taken; others pass to the page.
