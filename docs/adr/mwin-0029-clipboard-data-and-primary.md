# mwin-0029. Clipboard data and the primary selection

Status: Accepted

## Context

Programs copy more than text: an image editor copies pixels, an editor
copies text with its formatting. Every platform's clipboard holds data
by type underneath: Win32 by registered format, X11 by target atom,
Wayland by offered MIME type, Apple's systems by uniform type, the web
by MIME type in a `ClipboardItem` (plain text, HTML and PNG mandated).
SDL3 offers data by MIME type and reads one type back. Converting
images between formats (PNG to a Windows DIB) needs an image codec,
which a window library has no other use for. X11 and Wayland also keep
the primary selection, the text last selected, pasted with the middle
button, apart from the clipboard.

## Decision

- **Data by MIME type.** `mwinRequestClipboardWriteData` offers one to
  `MWIN_CLIPBOARD_ITEMS` (4) `mwinClipboardItem`s, each a MIME type of
  printable ASCII (a type and a subtype, at most
  `MWIN_CLIPBOARD_MIME`, 63, bytes) and its bytes, copied at the call
  within `clipboardBytes` in all; types match without regard to case
  and may not repeat. `mwinRequestClipboardReadData` asks for one type,
  and `mwinGetClipboardData` copies out what the last read found. A
  clipboard without that type completes the read with
  `mwin_outcomeFailed`, data past the limit with
  `mwin_outcomeTooLarge`; either leaves the data found before.
- **Bytes pass through.** No data is converted: a program puts PNG it
  encodes itself under `image/png`, and reads the bytes another program
  put there.
- **Text with data.** One item may be `text/plain` (with or without
  parameters), UTF-8, which becomes the clipboard's text, offered as
  the platform's text beside the other items and read with
  `mwinRequestClipboardRead`; a data read of `text/plain` is refused.
  A text write replaces the data, a data write the text, so the
  clipboard always holds what the last write put there.
- **The primary selection.** `mwinRequestPrimaryWrite` and
  `mwinRequestPrimaryRead` keep text apart from the clipboard, read
  back with `mwinGetPrimaryText`, checked and repaired as the
  clipboard's text is. Where the platform has none they answer
  `mwin_outcomeUnsupported`.
- **Platforms.** X11 converts a selection to the type's atom and keeps
  PRIMARY as it keeps CLIPBOARD; Wayland offers and receives the type,
  and keeps the primary selection through its protocol; Win32 registers
  a format per type, `PNG` for `image/png`; Apple's systems map the
  type to a uniform type; the web writes a `ClipboardItem`, a type
  outside the three mandated ones prefixed `web `; Android answers data
  requests `mwin_outcomeUnsupported`. Each backend in its own change.

## Consequences

A program copies an image with its text in one write and pastes by the
type it wants; nothing in the library grows an image codec. Windows
programs that read only `CF_DIB` see no image from a program that
writes PNG, and a program reading a DIB another program wrote reads it
by its registered name; that stays the program's to convert.
