# Clipboard and drag and drop

## The clipboard

The clipboard holds UTF-8 text, and data by MIME type beside it. Every operation is a request of a window, answered by a completion, because the platforms answer late or ask the user first: the browser may ask, iOS asks before reading another application's text, and on Wayland and Android only a focused window may use the clipboard.

- `mwinRequestClipboardWrite` puts text there. Text that is not UTF-8 is refused with `mwin_errorInvalid`, and text past `clipboardBytes` (1 MiB by default) with `mwin_errorCapacity`.
- `mwinRequestClipboardRead` reads it. When the read completes done, `mwinGetClipboardText` copies the text into the program's buffer; it takes the read's request from the completion record, so it never answers one read with another's text.
- Text from other programs has every ill-formed sequence replaced with U+FFFD, since a paste should not fail over one bad byte. Text past the limit completes the read with `mwin_outcomeTooLarge` rather than arriving cut short.

`mwinRequestClipboardWriteData` offers up to four items, each a MIME type and its bytes, one of which may be `text/plain` as the clipboard's text. `mwinRequestClipboardReadData` asks for one type and `mwinGetClipboardData` copies it out; a clipboard without the type completes the read failed. Bytes pass through unchanged: an image goes as PNG the program encodes, and a program reads the bytes another program put there. A text write replaces the data, and a data write the text. Android takes text only.

On X11 and Wayland, `mwinRequestPrimaryWrite`, `mwinRequestPrimaryRead` and `mwinGetPrimaryText` use the primary selection, the text last selected and pasted with the middle button, apart from the clipboard. Elsewhere they answer unsupported.

Two reads on one window in one frame paste once: the first is superseded. A getter answers `mwin_errorStale` for a read whose payload a later read of the same kind replaced.

## Drag and drop

While something is dragged over a window, the program hears `mwin_eventDragEntered`, `mwin_eventDragMoved` and `mwin_eventDragLeft`, with the place and what the drag holds (files, text). A drop brings `mwin_eventDropped` instead of the leaving. Windows take every drag of files or text; a program that does not want one ignores it.

The drop's payload waits under the drop's number from its record: `mwinGetDroppedFiles` copies out its paths, each UTF-8 and ended by a NUL, and `mwinGetDroppedText` its text, until the next drop makes that number stale. A path that is not UTF-8 names no file the program could open and is left out. Past `droppedFiles` (256) files or `dropBytes` (1 MiB) of paths or text, what does not fit is left out whole and the record's `truncated` says so.

A path names a file; nothing is granted with it. On the web a page sees file names, never paths. On iOS and Android, whose documents have no paths a program can keep, each dropped document is copied into the application's temporary or cache folder first, and the drop arrives once every copy is whole.
