# mwin-0038. The test backend tells text input and the keyboard

Status: Accepted

## Context

The test backend lets a library built on Maul Window check, without a
platform, what it asked of Maul Window: the clipboard it wrote
(`mwinTestGetClipboard`), the cursor it shows (`mwinTestGetCursor`).
It kept nothing of text input and the on-screen keyboard but whether a
window accepts text (`mwinWindowState.textInput`). A library that
places candidate windows by the caret, or shows the keyboard for a
field's purpose and hides it when the field is left, could not see
through Maul Window that it asked for the right thing; a mutant that
hid nothing survived in such a library's tests.

## Decision

- `mwinTestGetVirtualKeyboard(context, window, &visible, &purpose)`
  reads whether the keyboard shows and the purpose last asked for.
- `mwinTestGetTextInput(context, window, &enabled, &caret)` reads
  whether the window accepts text and the caret last given.
- Both tell what the test platform carried out: a request it answered
  with another outcome (`mwinTestSetAnswer`) leaves what they read. A
  window starts hidden with `mwin_purposeText`, not accepting text,
  with an empty caret; its record goes with it.

## Consequences

Libraries on Maul Window test the keyboard and the caret they ask for
as they test the clipboard and cursors. Two public functions, test
backend only.
