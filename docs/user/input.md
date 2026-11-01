# Input

Input belongs to a window and arrives in the event stream with the notifications, in the order the platform delivered it.

## Storage and resets

Each window keeps input in four classes, each with room for `inputPerWindow` records: discrete records (keys, text, buttons, touches and pen contacts beginning or ending), motion (cursor, touch and pen), raw deltas, and the wheel.

- **Discrete records are never merged.** When their storage is full, the record is lost and `mwin_eventInputStateReset` takes its place in the stream. The same record follows every loss of focus. After a reset no key or button counts as held and no composition is shown, so a program never acts on a held state it cannot trust: it drops any composition it shows, and the input method's next preedit shows one again.
- **Motion, deltas and wheel turns merge** into the newest waiting record of their kind when their storage is full: positions take the newer value, deltas and turns add, and `sampleCount` says how many platform records a record stands for.

## Keys

A key record names two things:

- its **code**, the physical key by its USB HID usage, named as the W3C `KeyboardEvent.code` values name it. `mwin_codeKeyQ` is the key right of Tab on every layout, so bindings by code survive a change of layout.
- its **key**, what the current layout makes of it: the code point it types with no modifier, or `MWIN_KEY_NAMED` for a key that types nothing.

`mwinContext_MapKeyCode` asks the layout what a code types, `mwinContext_GetKeyboardLayout` gives the layout's name, and `mwin_eventKeyboardLayoutChanged` says it changed. On iOS and Android a printing key's meaning is known only once it has been pressed under the current layout.

Some chords never reach a program: Alt+Tab and Control+Alt+Delete on Windows, Command+Tab on Apple's systems, Control+W in Chromium. `mwinContext_GetKeyReach` answers, for a key with modifiers, whether it is delivered, shared (the records arrive and the platform acts too, as Alt+F4 on Windows), uncertain (the desktop's configurable shortcuts may take it) or never delivered, so a rebinding screen can refuse or warn. The answers come from tables of what each platform keeps; a desktop may keep more.

## Text and input methods

Typed text is not keys. It arrives as `mwin_eventTextInput`, valid UTF-8, while the window accepts text: `mwinWindow_RequestTextInput` turns it on with the caret's rectangle, where the platform places the input method's candidate window. The text of a record stays valid until the frame that drained it returns.

An input method's composition arrives as `mwin_eventImePreedit` with its text, caret, selection and styled segments; a newer composition replaces one still waiting. Every offset lies on a character boundary within the text and the selection is in order, whatever the input method handed over, so the text can be sliced at any offset. While the input method composes, the keys it takes produce no key records, and no press arrives without its release. A reset ends the composition the program shows, not the input method's: the keys it takes stay left out until it ends its composition with an empty preedit (stopping text input ends it too) or the focus leaves.

On X11 the input method is IBus or Fcitx 5, reached over the session bus; without either, text comes from the keymap and its compose sequences. Wayland uses text-input-v3 and Windows its input methods through IMM32, the system drawing the candidate list.

`mwinWindow_RequestVirtualKeyboard` shows or hides the on-screen keyboard where there is one, with a purpose (text, number, e-mail, password, URL) that picks its layout. `MWIN_INPUT_PURPOSES` counts the purposes; a value from it on is not one, and the request fails. `mwin_eventVirtualKeyboardChanged` gives the part of the window it covers. On a Windows desktop with a hardware keyboard the request is denied, and Wayland shows its keyboard when text input is on. Android shows it only for this request, never by itself when the window gains the focus.

## The pointer

- `mwin_eventCursorMoved` is in the window's logical units; `mwin_eventCursorEntered` and `mwin_eventCursorLeft` frame it.
- Button records carry the count of quick clicks they complete, which follows the platform's double-click setting where it has one.
- The wheel turns in detents, fractional for touchpads and smooth wheels, with the user's scrolling direction left as it is.
- `mwin_eventRawPointerDelta` is the device's own motion. `mwinWindow_RequestCursorMode` hides, confines or captures the cursor; raw deltas keep coming while it is captured, as a first-person camera needs. macOS and the web cannot confine the pointer, and a page's pointer lock needs a user's gesture.

## Cursors

`mwinWindow_RequestCursorShape` picks one of the system's shapes. `mwinCreateCursor` makes a cursor from one to four RGBA images (the first at scale 1, each next one wider for higher scales, at most 128 pixels a side) and a hotspot; `mwinWindow_RequestCursorImage` shows it over a window, which takes the image for its scale. A shape and an image replace each other, and destroying a cursor in use leaves its windows the default shape. iOS shows no cursor and answers unsupported, though the cursor is still made, so portable code needs no branch.

## Touch and pens

Touches and pens are records of their own, never made into mouse records.

- **Touch:** `mwin_eventTouchDown`, `Moved`, `Up` and `Cancelled`, with an id stable for the touch's life, its place and its pressure where the screen measures it (-1 otherwise). X11 tells a program of no cancelled touch.
- **Pens:** `mwin_eventPenDown`, `Up` and `Moved` (touching or hovering) and `mwin_eventPenButtonDown` and `Up` for the barrel button, with pressure from 0 to 1, tilt in degrees and flags for the eraser end, contact and the barrel button.
