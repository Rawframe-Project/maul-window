# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and the behavior.

## [0.13.0](https://github.com/Rawframe-Project/maul-window/compare/v0.12.0...v0.13.0) (2026-11-05)


### ⚠ BREAKING CHANGES

* **drop:** on macOS, iOS and Android, a drop with a text item after the first, or on macOS with a pasteboard item that is neither a file nor text, is now marked truncated. On Android, so is a drop whose text found no room for its copy. On macOS, an item that carries a file URL no longer gives the drop text as well.
* **android:** on Android, a drag that enters the window while the last drop's documents are still copied supersedes that copy. The last drop is posted first, with the documents already whole and marked truncated, and the new drag's records follow. Such a drag was refused and not reported.

### Fixed

* **android:** let a drag that enters supersede a drop still copying ([#458](https://github.com/Rawframe-Project/maul-window/issues/458)) ([5c9e369](https://github.com/Rawframe-Project/maul-window/commit/5c9e369380bbad629b1a38e58580bf70a3f3ff07)), closes [#453](https://github.com/Rawframe-Project/maul-window/issues/453)
* **core:** give MWIN_NODISCARD its attribute under GCC 14's C23 ([#465](https://github.com/Rawframe-Project/maul-window/issues/465)) ([8c9eeef](https://github.com/Rawframe-Project/maul-window/commit/8c9eeef718c444a789a6616cef5619f89330eee7)), closes [#464](https://github.com/Rawframe-Project/maul-window/issues/464)
* **drop:** mark a drop truncated for every item it leaves out ([#461](https://github.com/Rawframe-Project/maul-window/issues/461)) ([c4e99bc](https://github.com/Rawframe-Project/maul-window/commit/c4e99bca169eb63b8d7cae8df49a02235eed2a3a)), closes [#455](https://github.com/Rawframe-Project/maul-window/issues/455)

## [0.12.0](https://github.com/Rawframe-Project/maul-window/compare/v0.11.0...v0.12.0) (2026-11-04)

The iOS device check, that a window shows no flash against the launch
screen before its first frame, is waived by the owner for this
release, which ships on the iOS simulator suite: its iOS changes leave
the launch and the first frame alone.

### Changed

- **Breaking:** `mwinRequest_GetDialogFiles` takes a `truncatedOut`
  argument after `countOut`. A dialog choice holding a file the
  platform cannot give as a path now completes done with the other
  paths and marked truncated; before, the whole dialog failed. Such a
  file is a path that is not UTF-8, a portal's choice that is no local
  file, an item with no file system path on Windows, or a document
  Android could not read or copy. A choice where no chosen file can be
  given as a path still fails. A choice past the `dialogFiles` or
  `dialogBytes` limits is still too large as a whole
  ([#452](https://github.com/Rawframe-Project/maul-window/pull/452)).
- **Breaking:** on Android, a drop of a document address the program
  cannot be granted no longer ends the program; before,
  `requestDragAndDropPermissions` threw and nothing caught it. The drop
  goes on without the drag's permissions. A drop with a document that
  cannot be opened or copied now brings the other documents and is
  marked truncated; before, it brought no files and was not marked
  ([#449](https://github.com/Rawframe-Project/maul-window/pull/449)).
- **Breaking:** on Android, a dropped or chosen document whose
  provider sends nothing for five seconds is left out, and the drop or
  the dialog's answer is marked truncated; before, the copy waited for
  it without end. The five seconds start again with each piece the
  provider sends. Destroying the window now stops a drop's or a
  dialog's copy
  ([#457](https://github.com/Rawframe-Project/maul-window/pull/457)).
- **Breaking:** on iOS, a drop with an item it does not deliver is
  now marked truncated: an item that is neither a file nor text, that
  failed to load, or whose file could not be copied. A text item that
  fails to load is left out rather than given as an empty text
  ([#454](https://github.com/Rawframe-Project/maul-window/pull/454)).

## [0.11.0](https://github.com/Rawframe-Project/maul-window/compare/v0.10.0...v0.11.0) (2026-11-04)

### Changed

- **Breaking:** on Windows, a generic HID gamepad gets its controls from
  the first 32 capabilities its descriptor gives of the Button page and
  the first 32 of the Generic Desktop page, whatever other pages it
  has. Before, a pad with more than 32 button capabilities got no
  buttons, and one with more than 32 value capabilities got no axes and
  no hats. Capabilities of other pages, such as a vendor's, counted
  toward the 32, so a descriptor that gave more than 32 of them ahead
  of its controls got none either. A pad left with no controls of
  either kind was not added as a gamepad
  ([#441](https://github.com/Rawframe-Project/maul-window/pull/441),
  [#443](https://github.com/Rawframe-Project/maul-window/pull/443)).
- Maul Window builds against Maul Unicode 1.0.0, which has the API and
  the results of 0.13.0, so nothing a program gets from Maul Window
  changes. The installed CMake config and pkg-config file ask for
  Maul Unicode 1.0.0, and a build on a machine with only Maul Unicode
  0.13 installed fetches 1.0.0 instead of using it
  ([#439](https://github.com/Rawframe-Project/maul-window/pull/439)).

## [0.10.0](https://github.com/Rawframe-Project/maul-window/compare/v0.9.0...v0.10.0) (2026-11-04)

### Changed

- **Breaking:** on Wayland, a drag that enters while a drop is still
  being read from the dragging program is refused, accepted as no type,
  and brings no records. Before, it brought `mwin_eventDragEntered` and
  its moves, the source was told the window would take it, and its drop
  brought `mwin_eventDragLeft` instead of `mwin_eventDropped`, since the
  reads belonged to the drop before it. The drop being read is
  delivered as before
  ([#428](https://github.com/Rawframe-Project/maul-window/pull/428)).
- **Breaking:** on X11 and Wayland, a compose sequence that types 64
  bytes or more types its text when it fits the window's text room
  (`textBytesPerWindow`), and posts `mwin_eventInputStateReset` after
  its key past it. Before, it typed nothing, with no reset. On X11, a key
  held for IBus's or Fcitx's answer keeps such a text in order behind
  the keys before it, and a key whose text was lost goes through the
  input method too, its reset posted after it
  ([#430](https://github.com/Rawframe-Project/maul-window/pull/430)).

## [0.9.0](https://github.com/Rawframe-Project/maul-window/compare/v0.8.1...v0.9.0) (2026-11-03)

### Changed

- **Breaking:** on X11, each clipboard or primary selection read uses a
  property of its own, the next of four, and takes only an answer that
  names its selection, target and property, or no property, and pieces
  only in that property. Before, a read took any answer sent to its
  window, so a late answer to a read that ran out of time answered the
  next read with the earlier read's data. An answer for another read is
  now left: the read waits for its own or fails after five seconds. A
  read that ran out of time or stopped taking pieces rests its property
  for a minute, and with all four resting a read fails at once
  ([#423](https://github.com/Rawframe-Project/maul-window/pull/423)).
- **Breaking:** on X11, each conversion of a drop goes into a property
  of its own, the next of four, and takes only an answer to the drop's
  window that names its target and property, or no property. Before,
  any answer while a drop ran was taken, so a late answer to an earlier
  drop delivered that drop's files or text with the next one. Another
  answer is now left: the drop waits for its own or is delivered
  truncated after five seconds. A drop that ran out of time, or whose
  answer came in pieces, rests its property for a minute, and with all
  four resting a drop is delivered at once with no files, no text and
  `truncated` set
  ([#426](https://github.com/Rawframe-Project/maul-window/pull/426)).

## [0.8.1](https://github.com/Rawframe-Project/maul-window/compare/v0.8.0...v0.8.1) (2026-11-03)

### Fixed

- On X11, a commit or a composition from IBus or Fcitx is now held up to
  the `textBytesPerWindow` limit. A commit past it posts
  `mwin_eventInputStateReset`; before, one past 1024 bytes was dropped
  with no reset. A composition past it posts the reset, then ends;
  before, one past 512 bytes ended without the reset
  ([#410](https://github.com/Rawframe-Project/maul-window/pull/410)).
- On Windows, a UTF-16 index the input method gives inside a surrogate
  pair, for a composition's caret, selection or clause, now ends before
  the pair, as on the other platforms; it fell after the pair. A
  composition string whose second read returns an error, no size or
  more than the first read said is refused; an error could have been
  taken as a count of about two billion units and read past the buffer
  ([#412](https://github.com/Rawframe-Project/maul-window/pull/412)).
- On the web with Emscripten, text another script pushes, a dropped
  file's name, the page's languages and a typed key's character turn
  each lone surrogate into U+FFFD, as text field strings and gamepad
  names do since 0.8.0; before, such a commit or file name was lost and
  the language list not taken. On the web, a page canvas whose id holds
  a lone surrogate, which no CSS selector can name, is refused: the
  window's creation answers `mwin_outcomeFailed`
  ([#413](https://github.com/Rawframe-Project/maul-window/pull/413)).
- On Windows, a HID gamepad whose input report does not fit a
  1024-byte raw input read is turned away; before, it was added and
  never moved, since none of its reports could be read
  ([#417](https://github.com/Rawframe-Project/maul-window/pull/417)).

## [0.8.0](https://github.com/Rawframe-Project/maul-window/compare/v0.7.0...v0.8.0) (2026-11-03)

The iOS device check, that a window shows no flash against the launch
screen before its first frame, is waived by the owner for this
release, which ships on the iOS simulator suite: its iOS changes leave
the launch and the first frame alone.

### Changed

- **Breaking:** Maul Window needs Maul Unicode 0.13.0. A build that
  provides Maul Unicode itself must provide 0.13.0. On macOS, iOS,
  Android and the web, a UTF-16 index an input method gives inside a
  surrogate pair, for a caret, a selection or a clause, now ends before
  the pair; on macOS and iOS it fell after the pair, on Android a
  selection's end did, and so did a caret on the web with Emscripten
  ([#334](https://github.com/Rawframe-Project/maul-window/pull/334),
  [#399](https://github.com/Rawframe-Project/maul-window/pull/399),
  [#408](https://github.com/Rawframe-Project/maul-window/pull/408)).
- **Breaking:** names settled for 1.0
  ([#384](https://github.com/Rawframe-Project/maul-window/pull/384),
  [#386](https://github.com/Rawframe-Project/maul-window/pull/386),
  [#387](https://github.com/Rawframe-Project/maul-window/pull/387),
  [#392](https://github.com/Rawframe-Project/maul-window/pull/392)):

  | 0.7.0 | 0.8.0 |
  |---|---|
  | `mwinEvent.type`, `mwinEventType` | `mwinEvent.kind`, `mwinEventKind` |
  | `mwinGamepad_GetMotion` | `mwinGamepad_TakeMotion` |
  | `mwinContext_HoldTestRequests` | `mwinContext_EnableTestHold`, read back with the new `mwinContext_IsTestHoldEnabled` |
  | `mwinContext_GetDroppedFiles`, `mwinContext_GetDroppedText` | `mwinDrop_GetFiles`, `mwinDrop_GetText` |
  | a drop's `uint32_t` number (`mwinDropEvent.drop`) | an `mwinDropId` |

  The event kinds keep their names and numbers. A drop id answers
  `mwin_errorStale` for the null id, another slot's id and a drop a
  later one replaced, as the number did.
- **Breaking:** `mwinWindow_RequestTextInput` takes no caret;
  `mwinWindow_RequestTextInputCaret` sets it, as its own request kind
  (`mwin_requestTextInputCaret`), so a newer caret supersedes an older
  one and never a text input request. A window keeps its caret while
  text input is off and on again. On Wayland, moving the caret no
  longer sends text-input-v3's `enable` again, which reset the input
  method's state on every move
  ([#393](https://github.com/Rawframe-Project/maul-window/pull/393)).
- **Breaking:** text lengths and composition offsets are `size_t`:
  `mwinTextEvent.length`, `mwinPreeditEvent.length`, `selectionStart`
  and `selectionEnd`, `mwinPreeditSegment.start` and `length`, and the
  web handles' `selectorLength` and `accessibilityLength`.
  `mwinPreeditEvent.caret` is `ptrdiff_t` and keeps -1 for a hidden
  caret ([#388](https://github.com/Rawframe-Project/maul-window/pull/388)).
- **Breaking:** `mwinRequest_GetDialogFiles` and
  `mwinContext_GetTestIcon` report their count in a `size_t*`, as the
  listing calls do
  ([#394](https://github.com/Rawframe-Project/maul-window/pull/394)).
- **Breaking:** `mwinPointerEvent.buttons` is an `mwinMouseButtons`
  (`uint32_t`, a named bit for each button, `mwin_mouseLeft` to
  `mwin_mouseForward`), `mwinGamepadState.buttons` an
  `mwinGamepadButtons` (`uint32_t`), and `mwinPenEvent.button` an
  `mwinPenButton` (`int32_t`, `mwin_penButtonNone` and
  `mwin_penButtonBarrel`). The pointer and pen records' layouts change
  ([#389](https://github.com/Rawframe-Project/maul-window/pull/389)).
- **Breaking:** the kinds, modes and choices of the public structs and
  calls are `int32_t`, and the sets of bit flags (`mwinModifiers`,
  `mwinWindowStyle`, `mwinPenFlags`, `mwinDragContents`,
  `mwinGamepadCapabilities`) are `uint32_t`. This changes the layout of
  the event, info and def structs. A negative kind is refused with
  `mwin_errorInvalid`, and counted as a misuse on a context; a cursor
  mode, window mode, window kind, dialog kind, hit region's kind,
  message box's kind or buttons, or test answer of `INT32_MIN` used to
  become 0 and go ahead. `mwinGamepad_SetTestButton` and
  `mwinGamepad_SetTestAxis` answer `mwin_errorInvalid` for a value
  below 0 or past 255
  ([#315](https://github.com/Rawframe-Project/maul-window/pull/315)).
- **Breaking:** four counts are named by the family's `thingCount` rule
  ([#315](https://github.com/Rawframe-Project/maul-window/pull/315)):

  | 0.7.0 | 0.8.0 |
  |---|---|
  | `mwinPointerEvent.clicks` | `mwinPointerEvent.clickCount` |
  | `mwinEvent.samples` | `mwinEvent.sampleCount` |
  | `mwinGamepadInfo.rawButtons` | `mwinGamepadInfo.rawButtonCount` |
  | `mwinGamepadInfo.rawAxes` | `mwinGamepadInfo.rawAxisCount` |

- **Breaking:** a getter that fills a caller's buffer or list leaves the
  bytes or ids past those it reports as they were when it succeeds; it
  used to zero them. A failure still zeroes the whole capacity. This
  covers `mwinRequest_GetClipboardText`, `GetClipboardData`,
  `GetPrimaryText` and `GetDialogFiles`; `mwinDrop_GetFiles` and
  `mwinDrop_GetText`; `mwinContext_GetKeyboardLayout`,
  `GetPreferredLocales`, `GetGamepads` and `GetMonitors`; and the test
  platform's `mwinWindow_GetTestTitle`, `mwinContext_GetTestClipboard`,
  `GetTestClipboardData`, `GetTestPrimary`, `GetTestOpened` and
  `GetTestDialog`. Text is never NUL-terminated, so a program reads
  only the length the call reports
  ([#313](https://github.com/Rawframe-Project/maul-window/pull/313)).
- **Breaking:** `mwinContext_NextEvent` and `mwinGamepad_TakeMotion`,
  refused with `mwin_errorBusy` because they were called from inside
  the context's allocator while another call uses the context, leave
  their output as it was; they used to zero it. The id of what a call
  makes (a window, a cursor, a request) is still the null id on every
  failure
  ([#290](https://github.com/Rawframe-Project/maul-window/pull/290)).
- Control characters never reach text or compositions. A committed tab
  arrives as Tab's press and release, and a line feed, a carriage
  return, or a carriage return followed by a line feed in the same
  commit as one Enter, with the text around them as text records, in
  order. Every other
  control character (C0, DEL, C1) is dropped from text and
  compositions, and a composition's caret, selection and segments move
  with the bytes that go. Before, the web, Wayland, IBus, Fcitx and the
  Win32 input method passed them on, NUL included, and Android and iOS
  took a carriage return and line feed as two Enters
  ([#339](https://github.com/Rawframe-Project/maul-window/pull/339),
  [#343](https://github.com/Rawframe-Project/maul-window/pull/343)).
- The preferred locales keep the longest beginning of the user's list
  that fits the `localeBytes` limit on every platform: the first tag
  that does not fit is left out with every tag after it, and a list
  whose first tag does not fit is empty. Windows hands over its list
  only whole, so there a list that does not fit keeps only its first
  language, the one Windows shows, where that fits. Before, Android,
  the web and Windows left a list past the limit as it was, or empty,
  without `mwin_eventLocaleChanged`. Linux held at most 256 bytes of
  tags whatever the limit, and let a later, shorter tag in after one
  that did not fit. When a desktop source named languages none of
  which fit, it took AccountsService's `Language` or the starting list
  instead; on Windows, a first language one byte past the limit kept
  the old list
  ([#366](https://github.com/Rawframe-Project/maul-window/pull/366),
  [#368](https://github.com/Rawframe-Project/maul-window/pull/368),
  [#370](https://github.com/Rawframe-Project/maul-window/pull/370),
  [#402](https://github.com/Rawframe-Project/maul-window/pull/402)).
- **Test platform:** `mwinContext_SetTestLocales` takes a list past the
  `localeBytes` limit as a platform's list, keeping its leading tags
  that fit; it answered `mwin_errorCapacity`
  ([#366](https://github.com/Rawframe-Project/maul-window/pull/366)).

### Added

- `mwinInitFunction`, `mwinFrameFunction` and `mwinQuitFunction` type
  `mwinAppDef`'s `init`, `frame` and `quit`, so a program can declare
  them; the def's layout is unchanged. `mwinShowMessageBox` says that a
  failure leaves `acceptedOut` as it was
  ([#385](https://github.com/Rawframe-Project/maul-window/pull/385)).

### Fixed

- **Android:** the on-screen keyboard showed by itself over a window
  that never asked for it, whenever the window gained the focus. In
  landscape it covered the window's middle, so touches meant for the
  program typed letters instead. It now shows only when the program
  asks for it
  ([#327](https://github.com/Rawframe-Project/maul-window/pull/327)).
- **Android:** asking for the on-screen keyboard, or for it with another
  purpose, while an input method composed (a hardware keyboard composes
  through it while the window accepts text) threw the composition away,
  and the program was not told it ended. The composition is now
  committed; only stopping text input drops it
  ([#330](https://github.com/Rawframe-Project/maul-window/pull/330)).
- **Android:** a document whose provider gave a name starting with
  U+0000 was lost, since its copy's file name was empty; a U+0000 later
  in the name cut it short. Each U+0000 now becomes `_`, as each `/`
  does
  ([#341](https://github.com/Rawframe-Project/maul-window/pull/341)).
- **Android:** a document name of a million UTF-16 units held the main
  thread for minutes while it was cut to 255 bytes, and a cut that
  ended at a lone low surrogate dropped one character too many
  ([#362](https://github.com/Rawframe-Project/maul-window/pull/362)).
- **Linux:** a file dialog run through zenity could hand the program
  other paths than the one chosen: a name ending in a line feed lost
  it, and a single choice was split at every byte 0x1E, so a path
  holding one came back as two files. A dialog whose only path went
  past the limits now answers `mwin_outcomeTooLarge`, on every backend,
  as the header says; it answered `mwin_outcomeFailed`
  ([#346](https://github.com/Rawframe-Project/maul-window/pull/346)).
- **Linux:** power, settings, file dialogs and input methods took their
  D-Bus signals from any program on the bus. Another local program
  could set `onBattery`, choose a portal dialog's files, change
  settings, or type text into the focused window. Each now hears only
  the service's owner
  ([#347](https://github.com/Rawframe-Project/maul-window/pull/347)).
- **Windows:** a drop read a medium the dragging program handed over in
  another form, such as a stream, as if it were global memory, and
  read a file list past `droppedFiles` at a cost growing with the
  square of its paths. A medium of another kind is now let go unread
  and the drop marked truncated
  ([#351](https://github.com/Rawframe-Project/maul-window/pull/351)).
- **Windows:** a drop lost its files or text when the data object
  answered with a success code other than `S_OK`, a path left out took
  a file's place, and a file list whose header pointed past its memory
  delivered nothing without marking the drop truncated. The file list
  is now read once, past paths left out, and checked against its
  memory
  ([#356](https://github.com/Rawframe-Project/maul-window/pull/356)).
- **Windows:** gamepad names were cut short or thrown away: a
  Windows.Gaming.Input name lost everything past its 21st character
  ("Xbox Wireless Control"), and a HID name of 22 or more characters
  of most non-Latin scripts became "HID gamepad". Names are now cut to
  whole characters within 64 bytes
  ([#364](https://github.com/Rawframe-Project/maul-window/pull/364)).
- **macOS and iOS:** clipboard and drop text was cut at a U+0000, and
  text with a lone surrogate was left out of a macOS drop, failed a
  clipboard read, and crashed the program in an iOS drop. Text is now
  taken by its UTF-16 units, U+0000 kept and a lone surrogate as U+FFFD
  ([#353](https://github.com/Rawframe-Project/maul-window/pull/353)).
- **iOS and macOS:** typed text with a lone surrogate was lost without
  a sign, and a composition with one was not shown; a U+0000 cut a
  composition short. Typed and composed text is now taken by its UTF-16
  units, and a reset is posted when there is no memory for it. A macOS
  drop now marks itself truncated when it leaves out a file whose path
  is not UTF-8, and a macOS or iOS file dialog fails a choice it cannot
  convert, instead of answering with fewer files
  ([#358](https://github.com/Rawframe-Project/maul-window/pull/358),
  [#360](https://github.com/Rawframe-Project/maul-window/pull/360)).
- **Web:** with Emscripten, typed or composed text holding a lone
  surrogate, which another script on the page, an input method or a
  device's name can give, was lost without a reset, and a gamepad's
  name was cut short at it. Each lone surrogate is now U+FFFD, as on
  every other platform and in the wasm32-wasi build
  ([#408](https://github.com/Rawframe-Project/maul-window/pull/408)).
- **X11:** a button event for button 0, which another program on the
  display can send, posted a press of no button that never ended. It is
  now ignored
  ([#395](https://github.com/Rawframe-Project/maul-window/pull/395)).
- A free function could read a block it had just freed. When a new
  payload replaced an old one (a clipboard read's text, data or primary
  text, a file dialog's files, a drop's files and text), the old block
  went to the allocator's free function while the getters still read
  it, and a getter called from there read freed memory. The replaced
  request or drop is now stale before its block goes back, so such a
  getter answers `mwin_errorStale`
  ([#303](https://github.com/Rawframe-Project/maul-window/pull/303)).
- While `mwinDestroyWindow` gave the window's blocks to the allocator's
  free function, `mwinWindow_GetState` and `mwinWindow_GetNativeHandles`
  called from there still answered for the window, and a cursor was
  still found while `mwinDestroyCursor` freed its images. The window's
  id is now stale before its blocks reach the free function, and the
  cursor's before its images do
  ([#309](https://github.com/Rawframe-Project/maul-window/pull/309)).
- After `quit`, the context's teardown gave its blocks to the
  allocator's free function while its windows, cursors and payloads
  were still found and the platform could still be reached; a call from
  there could read a freed block. Once `quit` returns, every window,
  cursor, request and drop id is stale there,
  `mwinContext_GetKeyReach` and `mwinContext_GetKeyboardLayout` answer
  `mwin_errorState`, and `mwinContext_MapKeyCode` answers no key
  ([#318](https://github.com/Rawframe-Project/maul-window/pull/318)).
- **Test platform:** `mwinWindow_GetTestTextInput` and
  `mwinWindow_GetTestVirtualKeyboard` left their outputs as they were
  when they failed; they clear them now
  ([#283](https://github.com/Rawframe-Project/maul-window/pull/283)).
- **Build:** a version bump did not reach an existing build directory.
  CMake did not configure again, so the build kept the old version in
  its package version file, its pkg-config file and its ABI version
  ([#324](https://github.com/Rawframe-Project/maul-window/pull/324)).

## [0.7.0](https://github.com/Rawframe-Project/maul-window/compare/v0.6.0...v0.7.0) (2026-10-29)

### Changed

- **Breaking:** Maul Window needs Maul Unicode 0.8.0. A build that
  provides Maul Unicode itself must provide 0.8.0
  ([#273](https://github.com/Rawframe-Project/maul-window/pull/273)).
- **Breaking:** the public functions are named by the Maul family's
  naming rule, with no aliases. A call on one object is
  `mwinObject_Verb...`, the object being the kind of its first id
  (`mwinWindow_RequestTitle`, `mwinGamepad_GetState`,
  `mwinRequest_GetClipboardText`); a call that takes no id acts on the
  context (`mwinContext_NextEvent`). Creation, destruction, defaults and
  calls on no object keep `mwinVerbNoun`, and the test platform's calls
  put `Test` after the verb. Their parameters and behavior are
  unchanged: code that calls them changes the names and nothing else
  ([#264](https://github.com/Rawframe-Project/maul-window/pull/264)).

  | 0.6.0 | 0.7.0 |
  |---|---|
  | `mwinTestAddGamepad` | `mwinAddTestGamepad` |
  | `mwinTestAddMonitor` | `mwinAddTestMonitor` |
  | `mwinGetDroppedFiles` | `mwinContext_GetDroppedFiles` |
  | `mwinGetDroppedText` | `mwinContext_GetDroppedText` |
  | `mwinGetGamepads` | `mwinContext_GetGamepads` |
  | `mwinGetKeyReach` | `mwinContext_GetKeyReach` |
  | `mwinGetKeyboardLayout` | `mwinContext_GetKeyboardLayout` |
  | `mwinGetContextMisuse` | `mwinContext_GetMisuse` |
  | `mwinGetMonitors` | `mwinContext_GetMonitors` |
  | `mwinGetPreferredLocales` | `mwinContext_GetPreferredLocales` |
  | `mwinGetSystemFacts` | `mwinContext_GetSystemFacts` |
  | `mwinTestGetClipboard` | `mwinContext_GetTestClipboard` |
  | `mwinTestGetClipboardData` | `mwinContext_GetTestClipboardData` |
  | `mwinTestGetDialog` | `mwinContext_GetTestDialog` |
  | `mwinTestGetIcon` | `mwinContext_GetTestIcon` |
  | `mwinTestGetOpened` | `mwinContext_GetTestOpened` |
  | `mwinTestGetPrimary` | `mwinContext_GetTestPrimary` |
  | `mwinTestHold` | `mwinContext_HoldTestRequests` |
  | `mwinMapKeyCode` | `mwinContext_MapKeyCode` |
  | `mwinNextEvent` | `mwinContext_NextEvent` |
  | `mwinTestPost` | `mwinContext_PostTestEvent` |
  | `mwinTestSetAnswer` | `mwinContext_SetTestAnswer` |
  | `mwinTestSetClipboard` | `mwinContext_SetTestClipboard` |
  | `mwinTestSetClipboardData` | `mwinContext_SetTestClipboardData` |
  | `mwinTestSetClipboardUtf16` | `mwinContext_SetTestClipboardUtf16` |
  | `mwinTestSetDialogFiles` | `mwinContext_SetTestDialogFiles` |
  | `mwinTestSetKeyReach` | `mwinContext_SetTestKeyReach` |
  | `mwinTestSetLocales` | `mwinContext_SetTestLocales` |
  | `mwinTestSetPrimary` | `mwinContext_SetTestPrimary` |
  | `mwinTestSetScale` | `mwinContext_SetTestScale` |
  | `mwinTestSetSystemFacts` | `mwinContext_SetTestSystemFacts` |
  | `mwinTestSetTime` | `mwinContext_SetTestTime` |
  | `mwinSetGamepadMotion` | `mwinGamepad_EnableMotion` |
  | `mwinGetGamepadInfo` | `mwinGamepad_GetInfo` |
  | `mwinGetGamepadMotion` | `mwinGamepad_GetMotion` |
  | `mwinGetGamepadState` | `mwinGamepad_GetState` |
  | `mwinTestGetRumble` | `mwinGamepad_GetTestRumble` |
  | `mwinTestGetTriggerRumble` | `mwinGamepad_GetTestTriggerRumble` |
  | `mwinTestGetMotionOn` | `mwinGamepad_IsTestMotionEnabled` |
  | `mwinTestGamepadMotion` | `mwinGamepad_PostTestMotion` |
  | `mwinSetGamepadRumble` | `mwinGamepad_SetRumble` |
  | `mwinTestGamepadAxis` | `mwinGamepad_SetTestAxis` |
  | `mwinTestGamepadButton` | `mwinGamepad_SetTestButton` |
  | `mwinTestChangeGamepad` | `mwinGamepad_SetTestInfo` |
  | `mwinSetGamepadTriggerRumble` | `mwinGamepad_SetTriggerRumble` |
  | `mwinResultName` | `mwinGetResultName` |
  | `mwinGetMonitorInfo` | `mwinMonitor_GetInfo` |
  | `mwinTestChangeMonitor` | `mwinMonitor_SetTestInfo` |
  | `mwinTestRemoveGamepad` | `mwinRemoveTestGamepad` |
  | `mwinTestRemoveMonitor` | `mwinRemoveTestMonitor` |
  | `mwinGetClipboardData` | `mwinRequest_GetClipboardData` |
  | `mwinGetClipboardText` | `mwinRequest_GetClipboardText` |
  | `mwinGetDialogFiles` | `mwinRequest_GetDialogFiles` |
  | `mwinGetPrimaryText` | `mwinRequest_GetPrimaryText` |
  | `mwinTestAskAccessibility` | `mwinWindow_AskTestAccessibility` |
  | `mwinGetNativeHandles` | `mwinWindow_GetNativeHandles` |
  | `mwinGetWindowState` | `mwinWindow_GetState` |
  | `mwinTestGetCursor` | `mwinWindow_GetTestCursor` |
  | `mwinTestGetTextInput` | `mwinWindow_GetTestTextInput` |
  | `mwinTestGetTitle` | `mwinWindow_GetTestTitle` |
  | `mwinTestGetVirtualKeyboard` | `mwinWindow_GetTestVirtualKeyboard` |
  | `mwinTestDrop` | `mwinWindow_PostTestDrop` |
  | `mwinRequestAccessibilityRoot` | `mwinWindow_RequestAccessibilityRoot` |
  | `mwinRequestAspectRatio` | `mwinWindow_RequestAspectRatio` |
  | `mwinRequestClipboardRead` | `mwinWindow_RequestClipboardRead` |
  | `mwinRequestClipboardReadData` | `mwinWindow_RequestClipboardReadData` |
  | `mwinRequestClipboardWrite` | `mwinWindow_RequestClipboardWrite` |
  | `mwinRequestClipboardWriteData` | `mwinWindow_RequestClipboardWriteData` |
  | `mwinRequestCursorImage` | `mwinWindow_RequestCursorImage` |
  | `mwinRequestCursorMode` | `mwinWindow_RequestCursorMode` |
  | `mwinRequestCursorShape` | `mwinWindow_RequestCursorShape` |
  | `mwinRequestFileDialog` | `mwinWindow_RequestFileDialog` |
  | `mwinRequestFocus` | `mwinWindow_RequestFocus` |
  | `mwinRequestHitRegions` | `mwinWindow_RequestHitRegions` |
  | `mwinRequestIcon` | `mwinWindow_RequestIcon` |
  | `mwinRequestKeepAwake` | `mwinWindow_RequestKeepAwake` |
  | `mwinRequestMode` | `mwinWindow_RequestMode` |
  | `mwinRequestOpacity` | `mwinWindow_RequestOpacity` |
  | `mwinRequestOpenUrl` | `mwinWindow_RequestOpenUrl` |
  | `mwinRequestPosition` | `mwinWindow_RequestPosition` |
  | `mwinRequestPrimaryRead` | `mwinWindow_RequestPrimaryRead` |
  | `mwinRequestPrimaryWrite` | `mwinWindow_RequestPrimaryWrite` |
  | `mwinRequestRevealFile` | `mwinWindow_RequestRevealFile` |
  | `mwinRequestSize` | `mwinWindow_RequestSize` |
  | `mwinRequestSizeLimits` | `mwinWindow_RequestSizeLimits` |
  | `mwinRequestStyle` | `mwinWindow_RequestStyle` |
  | `mwinRequestTextInput` | `mwinWindow_RequestTextInput` |
  | `mwinRequestTitle` | `mwinWindow_RequestTitle` |
  | `mwinRequestVirtualKeyboard` | `mwinWindow_RequestVirtualKeyboard` |
  | `mwinRequestVisible` | `mwinWindow_RequestVisible` |

- **Breaking:** a call on the context that fails leaves no value from
  before in its outputs: a value is zero, an id the null id and a
  request the null request, whether the call was refused, named an
  object that is gone, or was made from inside the context's allocator.
  Each out-parameter's documentation says what a failure leaves
  ([#269](https://github.com/Rawframe-Project/maul-window/pull/269)).
- **Breaking:** the fourteen calls that copy text or bytes out
  (`mwinRequest_GetClipboardText`, `mwinContext_GetDroppedFiles`,
  `mwinContext_GetKeyboardLayout`, `mwinContext_GetPreferredLocales`
  and the others) write nothing into a buffer too short: they answer
  `mwin_errorCapacity` with the length needed, and
  `mwinRequest_GetDialogFiles` also with the number of paths, and leave
  the buffer zeroed. They used to copy the part that fit, which could
  end inside a character or a path. A program that read that part must
  ask again with a buffer of the length given
  ([#269](https://github.com/Rawframe-Project/maul-window/pull/269)).
- **Breaking:** `mwinContext_GetGamepads` and `mwinContext_GetMonitors`
  answer `mwin_success` when the ids do not all fit, with the first
  ones written and the total in `countOut`. They answered
  `mwin_errorCapacity`; a program that tested for it compares the count
  with its capacity instead
  ([#269](https://github.com/Rawframe-Project/maul-window/pull/269)).

### Added

- `mwinGamepad_IsMotionEnabled` tells whether a gamepad's motion
  sensors are on
  ([#264](https://github.com/Rawframe-Project/maul-window/pull/264)).
- A program that builds Maul Unicode itself can read the version Maul
  Window is built and tested with, at configure time:
  `maul-window_UNICODE_VERSION` from the installed package, set even
  when the program's Maul Unicode is another version, and the cache
  variable `MAUL_WINDOW_UNICODE_VERSION` when Maul Window is built from
  source with `add_subdirectory` or FetchContent
  ([#277](https://github.com/Rawframe-Project/maul-window/pull/277)).

### Fixed

- **Android:** a mouse move could be lost. Android 11 can end a
  mouse's hovering between two of its moves and send the second as a
  hover enter, which posted nothing while the cursor was inside; it
  now moves the cursor
  ([#271](https://github.com/Rawframe-Project/maul-window/pull/271)).
- **Android:** after an activity started turned, the monitor could say
  for up to two seconds that the display was turned the other way and
  narrower than the window on it (Android 11). While the window does
  not fit in the display as last read, the display is read again every
  tenth of a second
  ([#275](https://github.com/Rawframe-Project/maul-window/pull/275)).

## [0.6.0](https://github.com/Rawframe-Project/maul-window/compare/v0.5.1...v0.6.0) (2026-10-29)

The device checks on real hardware are waived by the owner for releases
through 2026-10-29, so 0.6.0 ships on its other gates. The iOS check,
that a window shows no flash against the launch screen before its first
frame, runs against it in
[#254](https://github.com/Rawframe-Project/maul-window/issues/254).

### Changed

- **Breaking:** Maul Window needs Maul Unicode 0.6.0, against which
  its Android, web and Windows backends did not build. A build that
  provides Maul Unicode itself must provide 0.6.0
  ([#250](https://github.com/Rawframe-Project/maul-window/pull/250)).
- **Breaking:** an argument past a fixed bound with a named constant is
  refused with the new `mwin_errorLimit` (-9) instead of
  `mwin_errorInvalid`, and counts one misuse. The bound is checked on
  lengths and counts before any content is read. `mwinCreateWindow`
  refuses a def whose title passes the `titleBytes` limit with
  `mwin_errorCapacity`, as `mwinRequestTitle` already did. A program
  that tests for `mwin_errorInvalid` on these refusals must test for
  the new results
  ([#182](https://github.com/Rawframe-Project/maul-window/pull/182)).
- **Breaking:** the fourteen bounds refused with `mwin_errorLimit` are
  renamed with the prefix `MWIN_MAX_`, with their values unchanged and
  no aliases: `MWIN_ADDRESS_BYTES`, `MWIN_CANVAS_SELECTOR_BYTES`,
  `MWIN_CLIPBOARD_ITEMS`, `MWIN_CLIPBOARD_MIME`, `MWIN_CURSOR_IMAGES`,
  `MWIN_CURSOR_SIZE`, `MWIN_DIALOG_FILTER_BYTES`,
  `MWIN_DIALOG_FILTERS`, `MWIN_DIALOG_TITLE_BYTES`,
  `MWIN_HIT_REGIONS`, `MWIN_ICON_IMAGES`, `MWIN_ICON_SIZE`,
  `MWIN_MESSAGE_BYTES` and `MWIN_MESSAGE_TITLE_BYTES` are now
  `MWIN_MAX_ADDRESS_BYTES` and so on
  ([#186](https://github.com/Rawframe-Project/maul-window/pull/186)).
- **Breaking:** a call on a context from inside that context's
  allocator, which `mwinRequestClipboardWrite`, `mwinRequestIcon` and
  `mwinRequestFileDialog` run partway through, is refused with the new
  `mwin_errorBusy` (-10) and counts one misuse. Every public function
  that takes a non-const context refuses it. Such a call used to go
  ahead, and could destroy the window, take a request slot or drain the
  queue while the outer call was still using them
  ([#198](https://github.com/Rawframe-Project/maul-window/pull/198)).

### Added

- `MWIN_INPUT_PURPOSES` counts the input purposes: every purpose is
  below it. `mwinRequestVirtualKeyboard` refuses a purpose from it on
  ([#228](https://github.com/Rawframe-Project/maul-window/pull/228)).

### Fixed

- A window's `mwin_eventInputStateReset` ends its composition:
  `composing` goes false, and the input method's next preedit starts
  one again. After a composition that did not fit the window's text
  storage, or one lost to full storage, the program used to keep
  drawing the old one, and the
  character keys that went down were left out as a composition's keys
  until a later preedit
  ([#194](https://github.com/Rawframe-Project/maul-window/pull/194)).
- After such a reset, the character keys the input method still takes
  for its composition stay left out, and the program reads the input
  method's empty preedit when its composition ends. They used to reach
  the program as presses and releases until the input method's next
  preedit
  ([#199](https://github.com/Rawframe-Project/maul-window/pull/199)).
- On Wayland, the input method's committed and composed text that is
  not UTF-8 is repaired with U+FFFD. Committed text
  used to be lost without a trace, and such a composition dropped,
  leaving the previous one showing
  ([#188](https://github.com/Rawframe-Project/maul-window/pull/188)).
- On iOS, a window takes touches before it first draws: it is black
  from its creation. Its touches used to go to the home screen behind
  it until its first frame was presented, and always in a simulator
  without a Metal device
  ([#197](https://github.com/Rawframe-Project/maul-window/pull/197)).
- On Linux, the motion samples of a device that stamps its reports
  (`MSC_TIMESTAMP`) are timed on the records' clock, as
  `mwinGamepadMotion.timeNs` is documented. Their time used to count
  from the device's first stamp
  ([#164](https://github.com/Rawframe-Project/maul-window/pull/164)).
- On Linux, after the kernel drops a device's events, a pad and its
  motion sensors skip what is left of the cut report and read the
  device's state again. A motion axis whose change was lost used to
  keep its old value until it changed again, and a pad posted its state
  mixed with the rest of the cut report
  ([#170](https://github.com/Rawframe-Project/maul-window/pull/170)).
- On Linux, a pad whose buttons cannot be read again after such a drop,
  as when the device has gone, posts nothing for it. Every button it
  held used to be posted as released
  ([#173](https://github.com/Rawframe-Project/maul-window/pull/173)).
- On Linux and on Windows (a HID pad read through Raw Input), a
  gamepad connected while every slot of `limits.gamepads` was taken is
  added once a slot is free. It used to be added only when plugged in
  again
  ([#213](https://github.com/Rawframe-Project/maul-window/pull/213),
  [#208](https://github.com/Rawframe-Project/maul-window/pull/208)).
- On Android 12 and later, each of a gamepad's two motors runs at its
  own strength. Both used to run at the strength of one of them.
  Android 11's single vibrator still runs at the stronger of the two
  ([#224](https://github.com/Rawframe-Project/maul-window/pull/224)).
- On Android, a gamepad whose motion sensors Android finds a moment
  after adding it gains motion, told by `mwin_eventGamepadChanged`. It
  used to never offer motion, and `mwinSetGamepadMotion` answered
  unsupported
  ([#225](https://github.com/Rawframe-Project/maul-window/pull/225)).
- On Android 11, a gamepad Android first read half merged is found: the
  library reads a new device half a second after it is first listed,
  and a pad still read part way is added at its first input, with
  Android's standard gamepad axes. Such a pad used to stay invisible to
  the program for as long as it ran
  ([#227](https://github.com/Rawframe-Project/maul-window/pull/227)).
- On Android, a stop of a gamepad's rumble, asked for or made as the
  context ends, stops the motors. After Android changed the device, as
  when it found a pad's motion sensors, the stop used to return
  `mwin_success` and leave the motors running until the time first
  asked for ran out
  ([#240](https://github.com/Rawframe-Project/maul-window/pull/240)).

## [0.5.1](https://github.com/Rawframe-Project/maul-window/compare/v0.5.0...v0.5.1) (2026-10-27)

### Fixed

- On X11 and Wayland, a drag over a window the program destroys ends
  with the window: none of its records reach the window that takes its
  place, and a drop still being gathered for it is refused to its
  source instead of being delivered to that window
  ([#154](https://github.com/Rawframe-Project/maul-window/pull/154)).
- On X11 and Wayland, a drag still reported when another enters is
  reported left first, so a window no longer hears two
  `mwin_eventDragEntered` without a leave between them, as when an X11
  source ended without a leave or another program started a drag
  ([#156](https://github.com/Rawframe-Project/maul-window/pull/156)).
- A gamepad's logical ranges are computed in 64 bits: an axis whose
  device range spans all 32 bits normalizes correctly on Linux, Android
  and Windows, and a Windows HID hat with such a range reads its step
  instead of centred
  ([#159](https://github.com/Rawframe-Project/maul-window/pull/159)).

## [0.5.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.5.0) (2026-10-25)

### Changed

- **Breaking:** `mwinMonitorInfo.variableRefresh` is an `mwinTristate`
  (`mwin_yes`, `mwin_no`, or `mwin_unknown` where the platform does not
  tell) instead of a `bool`. A program testing it as a truth value must
  compare it with `mwin_yes`. `mwinTristate` is now declared in
  `base.h`. X11 tells it from `vrr_capable`, macOS 12 and later from
  whether the screen's refresh interval can vary, iOS by a refresh
  above 60 Hz, and Android 16 from `hasArrSupport`. Win32, Wayland, the
  web, older macOS and Android, and an X11 output without
  `vrr_capable` report unknown, where they used to report no
  ([#116](https://github.com/Rawframe-Project/maul-window/pull/116)).

### Added

- On Windows, a window is reported occluded (`mwin_eventOccluded`)
  while it is minimized, cloaked by DWM (as on another virtual
  desktop), while the session is locked or while the display is off,
  and revealed when none of these holds; a dimmed display counts as on.
  `mwinGetWindowState` says the same. Coverage by other windows is not
  reported, since Windows tells it nowhere
  ([#117](https://github.com/Rawframe-Project/maul-window/pull/117)).

### Fixed

- On iOS and Android, a window made where it cannot be seen starts out
  occluded: on iOS one that takes a scene already in the background, on
  Android one made while the activity is stopped and its surface is
  still there (a path the Android emulator does not reach). It used to
  say it
  could be seen until the platform next changed its state
  ([#115](https://github.com/Rawframe-Project/maul-window/pull/115)).

## [0.4.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.4.0) (2026-10-25)

### Added

- On iOS, a window is reported occluded (`mwin_eventOccluded`) when
  its scene goes to the background, and revealed
  (`mwin_eventRevealed`) when the scene comes back to the foreground;
  `mwinGetWindowState` says the same. The program hears of it in the
  frame that tells of the suspending, after the suspending record, and
  of the revealing after the resumed record. An inactive scene, under
  Control Center, the app switcher or a system alert, is still seen and
  stays revealed
  ([#110](https://github.com/Rawframe-Project/maul-window/pull/110)).

## [0.3.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.3.0) (2026-10-25)

### Added

- On Android, a window is reported occluded (`mwin_eventOccluded`)
  while its activity is stopped, and revealed (`mwin_eventRevealed`)
  when the activity starts again; `mwinGetWindowState` says the same.
  The program hears of it in the frame that tells of the suspending,
  after the suspending record, and of the revealing after the resumed
  record. A pause alone, as under a translucent activity, in split
  screen or in picture-in-picture, leaves the window revealed
  ([#109](https://github.com/Rawframe-Project/maul-window/pull/109)).

## [0.2.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.2.0) (2026-10-25)

### Added

- On X11, a window wholly covered by other windows is reported
  occluded (`mwin_eventOccluded`), and revealed (`mwin_eventRevealed`)
  once any part of it can be seen again, as the X server computes it;
  `mwinGetWindowState` says the same. Under a compositing window
  manager the X server reports no window covered, so there a window is
  never occluded. The windows guide now lists when each platform
  reports occlusion
  ([#107](https://github.com/Rawframe-Project/maul-window/pull/107)).

## [0.1.1](https://github.com/Rawframe-Project/maul-window/compare/v0.1.0...v0.1.1) (2026-10-25)

### Fixed

- On Windows, input records (keys, typed text, the mouse, the wheel,
  raw input, touch and pen) carry the time their message happened, as
  `timeNs` promises, instead of the time the library handled it: input
  that waited behind a long frame or a modal loop is no longer stamped
  late by that wait. Touch and pen take the time Windows recorded with
  the sample. Input times never go backwards, across all kinds of input
  ([#102](https://github.com/Rawframe-Project/maul-window/pull/102)).

## [0.1.0](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.1.0) (2026-10-24)

### Changed

- `mwinMessageBoxDef` carries the ABI version, like the application
  def, and `mwinShowMessageBox` refuses a def from other headers with
  `mwin_errorVersion`. A program must rebuild against the new headers
  and build its def with `mwinDefaultMessageBoxDef()`, which now sets
  the version; a def filled in field by field must also set
  `version = MWIN_ABI_VERSION`
  ([#31](https://github.com/Rawframe-Project/maul-window/pull/31)).
- On iOS, a window created when no scene is free completes
  `mwin_outcomeUnsupported` where the application cannot show several
  scenes (an iPhone, or an Info.plist without
  `UIApplicationSupportsMultipleScenes`), instead of
  `mwin_outcomeFailed`
  ([#69](https://github.com/Rawframe-Project/maul-window/pull/69)).

### Added

- iPadOS windows: each further window gets a scene of its own, which
  the system shows side by side or in Stage Manager. Creating one can
  take up to ten seconds, and completes `mwin_outcomeFailed` if the
  system refuses or no scene comes. A scene the user closes asks its
  window to close, and a window the program destroys while others remain
  closes its scene
  ([#69](https://github.com/Rawframe-Project/maul-window/pull/69)).
- A hovering Apple Pencil is reported on iPadOS as pen motion without
  contact, with its tilt on iOS 16.4 and later, as the other platforms
  report a hovering pen
  ([#62](https://github.com/Rawframe-Project/maul-window/pull/62)).
- On Linux the preferred locales follow the desktop while the program
  runs: a language changed in GNOME's settings or in KDE Plasma's
  `plasma-localerc` replaces the list, and `mwin_eventLocaleChanged`
  says so. The list still starts from the program's environment, and
  without a system bus or a configuration folder it simply stays
  ([#67](https://github.com/Rawframe-Project/maul-window/pull/67)).

### Fixed

- On Linux a desktop portal or power service answering with values of
  an unexpected type no longer ends the program, and a text scale that
  is infinite or out of range no longer reaches it
  ([#32](https://github.com/Rawframe-Project/maul-window/pull/32)).
- On X11 a key held for the input method's answer reaches only the
  window it was pressed in, not a new window made in its place
  ([#34](https://github.com/Rawframe-Project/maul-window/pull/34)).
- On the web, records, strings, drops and clipboard text from the page
  are checked and repaired before the program sees them, so another
  script on the page can no longer crash the program or hand it
  positions, pressures, sizes or codes outside the contract
  ([#41](https://github.com/Rawframe-Project/maul-window/pull/41)).
- On Android 11 the on-screen keyboard asked for as an activity starts
  now shows, rather than depending on when input starts on the view
  ([#47](https://github.com/Rawframe-Project/maul-window/pull/47)).
- On Wayland a minimize that supersedes a waiting maximize or
  fullscreen request no longer lets a later configure answer another
  request with that mode's outcome
  ([#56](https://github.com/Rawframe-Project/maul-window/pull/56)).
- On iOS destroying a window whose creation failed no longer crashes,
  and a scene the system closed is never given to the next window
  ([#69](https://github.com/Rawframe-Project/maul-window/pull/69)).

## [0.0.1](https://github.com/Rawframe-Project/maul-window/releases/tag/v0.0.1) (2026-10-23)

The first release: windows, input, gamepads and platform services
behind one contract on Windows, macOS, iOS, Android, Wayland, X11 and
the web, with a headless test backend for programs' own tests.

### Added

- A program model in which every change to a window is a request
  answered by exactly one completion, everything the platform reports
  arrives in one ordered stream, no library thread runs, and all memory
  is taken when the context is created within named limits.
- Windows with their modes, styles, size limits, aspect ratio, opacity
  and icons; owned windows, menus and tooltips; custom chrome with hit
  regions that move and resize through the platform, and Windows 11's
  snap layouts; monitors with their scale, refresh rate, variable
  refresh and HDR facts; the native handles a GPU layer needs, and lost
  and restored surfaces on Android and the web.
- Keys by physical position and by the layout's meaning, which chords
  the platform keeps, text and input method compositions (IBus and
  Fcitx 5 on X11), the on-screen keyboard with an input purpose, mouse,
  wheel, raw motion and pointer capture, system and image cursors,
  touch, and pens with pressure, tilt, eraser and barrel button.
- Gamepads mapped by place through the platform or SDL_GameControllerDB,
  or raw, with batteries, rumble, trigger rumble and motion sensors;
  Windows.Gaming.Input, XInput and HID pads on Windows, evdev on Linux,
  GameController on macOS and iOS, Android's input devices and the
  Gamepad API.
- The clipboard (text, data by MIME type, the primary selection on X11
  and Wayland), drag and drop of files and text, file dialogs, message
  boxes (including one drawn on X11 where neither zenity nor kdialog is
  installed), opening addresses, revealing files and keeping the display
  awake.
- The user's theme, accent, reduced motion, text scale, power source,
  power saving and preferred languages, with change notifications.
- Hooks for accessibility adapters: the platform's root on Windows,
  macOS, iOS and Android (with touch exploration), and a host for ARIA
  elements on the web.
- The web backend with Emscripten, or as plain wasm32-wasi with a
  generated JavaScript module of imports.
- An application def stamped with the version of the headers it was
  built with, which `mwinRun` refuses from another release.
- A headless test backend that plays any platform report and tells what
  a program asked of the platform.
- `java/proguard-rules.pro` for Android applications that shrink their
  code with R8 or ProGuard.
