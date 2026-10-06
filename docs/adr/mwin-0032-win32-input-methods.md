# mwin-0032. Input methods on Win32 through IMM32

Status: Accepted

## Context

The requirements ask for "TSF (IMM32 fallback)" on Win32. Every
current Windows input method is a TSF text service; Windows has
blocked input methods built on IMM32 since Windows 8. A program that
implements no TSF text store still receives them: since Vista, TSF
bridges compositions into IMM32 messages (`WM_IME_COMPOSITION` with
the string, its attributes and caret). A text store (`ITextStoreACP2`)
would add UILess mode (the program draws the candidate list, which
exclusive fullscreen needs), reconversion and surrounding text, and
per-range layout queries, at the cost of a large COM surface with a
lock protocol. SDL 3, GLFW and winit use IMM32; Chromium and Firefox,
which edit whole documents, implement text stores.

## Decision

- The Win32 backend reads input methods through IMM32 messages and
  places the composition and candidate windows at the caret; it
  implements no TSF text store. TSF services compose; IMM32 is how
  the program hears them.
- This is revisited if the library gains an exclusive fullscreen mode
  or a contract for program-drawn candidate lists, surrounding text
  or reconversion.
- Input scopes need no text store (`SetInputScope`); the input purpose
  on Win32 is handled apart from this decision.

## Consequences

Every part of the input method contract (composition with segments,
caret and selection, commit, candidate placement) works with every
current input method, the emoji panel and voice typing. A program
cannot draw its own candidate list on Windows, which over a borderless
fullscreen window it does not need: the system draws it.
