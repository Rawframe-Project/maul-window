# Maul Window API reference

Generated from the public headers by `tools/gen_api.py`. The headers
are the source of truth; this file mirrors them.

## `base.h`

The base of the Maul Window API: the library version, the export and attribute macros, and the result codes every fallible function returns.

```c
mwinVersion mwinGetVersion(void);
```
Returns the version of the library that was linked, which may differ from the MWIN_VERSION macros a program was compiled with.  @return The library version. @par Thread safety Safe from any thread.

```c
const char* mwinResultName(mwinResult result);
```
Returns the name of a result code, for diagnostics.  @param result  Any value; an unknown one is named as such. @return A static, NUL-terminated string such as "mwin_errorCapacity". @par Thread safety Safe from any thread.

## `accessibility.h`

The hooks a program's accessibility adapters need from its windows. The library knows nothing of the tree. It hands the platform the root the program implements, tells the program when a client first asks for a window's tree, and on the web keeps an element next to the canvas for the program's ARIA elements (its selector is in mwinNativeHandles). On Linux, AT-SPI is a service of the application on the session's accessibility bus, and asks nothing of the window system. Its adapter takes the window's place, size and focus from the window's state and events.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestAccessibilityRoot(mwinContext* context, mwinWindowId window, void* root, mwinRequestId* requestOut);
```
Hands the platform's accessibility clients the root of the window's tree, which the program's adapter implements, or no root.  On Win32 the root is an IRawElementProviderSimple*. The window answers WM_GETOBJECT for UiaRootObjectId with it, and UI Automation takes references of its own. The provider must stay valid while it is the window's root. When the window is destroyed, UI Automation is told to let go of it.  On macOS the root is an object of the NSAccessibility protocol (an NSAccessibilityElement, say), whose accessibility parent is the window's view (mwinNativeHandles). The view gives it as its child and asks it what is focused and what is under a point; the window holds a reference to it while it is the root.  X11, Wayland and the web take no root and answer mwin_outcomeUnsupported.  @param context    The context. @param window     The window. @param root       The root, or NULL for none. @param requestOut Receives the request's id. May be NULL. @return `mwin_success`; `mwin_errorCapacity` when the window has its limit of requests in flight; `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL context. @par Thread safety Main thread only.

## `clipboard.h`

The clipboard, as UTF-8 text. Writing and reading are requests of a window, answered by a completion like any other: the browser's clipboard answers later and may ask the user first, and on Wayland only a focused window may use it. A later request of the same kind on the same window supersedes a waiting one, so two reads in one frame paste once unless the program counts the superseded read. A read that completes with mwin_outcomeDone leaves the text it found in the context, where mwinGetClipboardText copies it out until the next read is done; a read refused or too large leaves it. Text another program put there is checked before the program sees it: ill-formed UTF-8 has each maximal ill-formed subpart replaced with U+FFFD, and text past the clipboardBytes limit, after that, completes the read with mwin_outcomeTooLarge. An empty clipboard, or one without text, reads as empty text. What the program wrote stays in the context while the platform may ask for it, until the next write or the context's end.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestClipboardWrite(mwinContext* context, mwinWindowId window, const char* text, size_t length, mwinRequestId* requestOut);
```
Asks to put text on the clipboard.  @param context    The context. @param window     The window asking, which should have focus. @param text       UTF-8, not NUL-terminated; copied before the call returns. May be NULL when length is 0. @param length     Its bytes. @param requestOut Receives the request's id. May be NULL. @return `mwin_success`; `mwin_errorCapacity` for text past the clipboardBytes limit, when the context cannot hold it, or when the window has its limit of requests in flight; `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL context or text that is not UTF-8. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestClipboardRead(mwinContext* context, mwinWindowId window, mwinRequestId* requestOut);
```
Asks for the clipboard's text; when the request completes with mwin_outcomeDone, mwinGetClipboardText has it.  @param context    The context. @param window     The window asking, which should have focus. @param requestOut Receives the request's id. May be NULL. @return `mwin_success`; `mwin_errorCapacity` when the window has its limit of requests in flight; `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL context. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetClipboardText(const mwinContext* context, char* buffer, size_t capacity, size_t* lengthOut);
```
Copies out the text found by the last clipboard read that completed with mwin_outcomeDone; empty before any.  @param context    The context. @param buffer     Receives the text in UTF-8, not NUL-terminated. May be NULL when capacity is 0. @param capacity   The bytes buffer holds. @param lengthOut  Receives the text's length in bytes. @return `mwin_success`; `mwin_errorCapacity` when the text does not fit (the bytes that fit are written); `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

## `context.h`

The context and the run function. A program describes itself in an application def (three functions and the context's def) and hands it to mwinRun, which creates the context, calls init, calls frame once per frame, calls quit and destroys the context. On Win32, X11 and Wayland mwinRun pumps the platform itself; on the web and on mobile platforms the platform's loop drives it. The program cannot tell the two apart. Every limit on memory and work is named in mwinLimits, with a default a program may change. Everything the context needs is allocated when it is created, through the def's allocator.

```c
mwinContextDef mwinDefaultContextDef(void);
```
Returns the default context def: the default limits (8 windows; per window 32 requests, 256 notifications, 256 input records per class and 4,096 bytes of text; 1,024 title bytes; 16 monitors; 256 bytes of locales; 8 gamepads; 1 MiB of clipboard text; 256 files and 1 MiB of paths or text per drop), the C library's allocator and the native backend.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
mwinAppDef mwinDefaultAppDef(void);
```
Returns the default application def: the default context def and no functions.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRun(const mwinAppDef* def);
```
Runs a program: creates the context, calls init, then frame until a frame returns mwin_frameStop, then quit, and destroys the context. Where the platform owns the loop (the web, iOS) this function may never return, or, on the web without Emscripten, return `mwin_success` once init has succeeded while the page's frames run the program on; either way put cleanup in quit. On iOS init runs when the application's first scene connects, and a program that stops or fails there ends with quit while the application runs on. It is never called from inside a running program's functions.  @param def  The program: a valid cookie, init and frame set. @return init's status when it failed; `mwin_success` after a stop; `mwin_errorCapacity` when the allocator cannot give the context its memory; `mwin_errorUnsupported` for a backend this build lacks; `mwin_errorPlatform` when the window system cannot be reached; `mwin_errorInvalid` for a missing function, a bad cookie or a zero limit. @par Thread safety Main thread only.

```c
uint64_t mwinGetContextMisuse(const mwinContext* context);
```
Returns how many calls the context has refused as invalid input (`mwin_errorInvalid`): a count release builds can watch to catch a program's bugs. Stale ids are not misuse.  @param context  The context. @return The count; 0 for a NULL context. @par Thread safety Main thread only.

## `dialog.h`

File dialogs: open one file or many, save one, or choose a folder, as requests of a window answered by a completion like any other. The window stays live while the dialog shows, and frames go on. A dialog chosen from completes with mwin_outcomeDone, and its paths wait under the request's id for mwinGetDialogFiles until the next dialog completes; one the user closes completes with mwin_outcomeCancelled; a choice past the dialogFiles or dialogBytes limits with mwin_outcomeTooLarge. A page names no files, so the web has no dialogs (mwin_outcomeUnsupported).

```c
mwinFileDialogDef mwinDefaultFileDialogDef(void);
```
Returns the default file dialog def: a dialog to open one file, with the platform's title and folder and no filters.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestFileDialog(mwinContext* context, mwinWindowId window, const mwinFileDialogDef* def, mwinRequestId* requestOut);
```
Asks for a file dialog over a window. Its text is copied at the call.  @param context    The context. @param window     The window the dialog belongs to. @param def        The dialog. @param requestOut Receives the request's id, which names its paths. May be NULL. @return `mwin_success`; `mwin_errorCapacity` when the context cannot hold the copy or the window has its limit of requests in flight; `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL context or def, an invalid def, text that is not UTF-8, holds a NUL or passes its limit (the folder and name MWIN_ADDRESS_BYTES), a folder that is not absolute, a filter without a name or extensions, or an extension with a dot, '*' or '?'. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetDialogFiles(const mwinContext* context, mwinRequestId request, char* buffer, size_t capacity, size_t* lengthOut, uint32_t* countOut);
```
Copies the paths a dialog chose out: absolute, UTF-8, each ended by a NUL.  @param context   The context. @param request   The dialog's request, done. @param buffer    Receives the paths. May be NULL when capacity is 0. @param capacity  Its bytes. @param lengthOut Receives the bytes of the paths. @param countOut  Receives how many paths there are. May be NULL. @return `mwin_success`; `mwin_errorCapacity` with the bytes needed when they do not fit, the buffer filled as far as it goes; `mwin_errorStale` for a request that did not complete with mwin_outcomeDone or whose paths a later dialog replaced; `mwin_errorInvalid` for a NULL context or lengthOut. @par Thread safety Main thread only.

## `drop.h`

Drag and drop onto windows. While something is dragged over a window the program hears mwin_eventDragEntered, mwin_eventDragMoved (merged when motion waits) and mwin_eventDragLeft, so it can show where a drop would land; a drop brings mwin_eventDropped instead of the leaving. Windows take every drag that carries files or text. A drop's files and text wait in the context under the drop's number until the next drop, where these functions copy them out. Files come as their paths in UTF-8, each ended by a NUL; on the web, where a page never sees paths, as their names. A path that is not UTF-8 is left out, since no program could name the file with it, and text from another program has each maximal ill-formed subpart replaced with U+FFFD. Files past the droppedFiles limit, or paths past dropBytes, are left out whole, and text past dropBytes is left out; the drop's record says when anything was. A path names a file; it grants no access the program did not already have.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetDroppedFiles(const mwinContext* context, uint32_t drop, char* buffer, size_t capacity, size_t* lengthOut);
```
Copies out the paths of a drop's files, each ended by a NUL.  @param context    The context. @param drop       The drop's number, from its record. @param buffer     Receives the paths. May be NULL when capacity is 0. @param capacity   The bytes buffer holds. @param lengthOut  Receives the paths' length in bytes, NULs included. @return `mwin_success`; `mwin_errorCapacity` when they do not fit (the bytes that fit are written); `mwin_errorStale` for a drop a later one replaced; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetDroppedText(const mwinContext* context, uint32_t drop, char* buffer, size_t capacity, size_t* lengthOut);
```
Copies out the text of a drop.  @param context    The context. @param drop       The drop's number, from its record. @param buffer     Receives the text in UTF-8, not NUL-terminated. May be NULL when capacity is 0. @param capacity   The bytes buffer holds. @param lengthOut  Receives the text's length in bytes. @return As mwinGetDroppedFiles. @par Thread safety Main thread only.

## `event.h`

The event stream. The program drains it in frame with mwinNextEvent, which returns every record in the order it arrived, across windows. Records wait in storage per window, each with its own limit, so one busy window cannot push out another's records. Notifications that must be handled before the platform goes on (the application's lifecycle, a lost or restored surface) come before all others; when the platform waits for the program to handle one, the library runs a frame at once, from inside the platform's call, so the program sees it in time. The context's own notifications carry the null window id. A request's completion comes after the notifications the change caused: a size request is answered after mwin_eventResized. Input comes in four classes, each with its own storage per window. Discrete records (keys, text, buttons, touches and pen contacts beginning or ending) are never merged: when their storage is full the window gets mwin_eventInputStateReset instead, which also follows every loss of focus, after which no key or button counts as held. Motion, raw deltas and the wheel are delivered sample by sample while there is room, and merged into the newest waiting record of their kind when there is not; samples says how many a record stands for. Text in a record stays valid until the frame that drained it returns. Gamepads' records are the context's, in storage of their own: their buttons never merge, and a gamepad whose button records were lost gets mwin_eventInputStateReset; an axis moving when its storage is full merges into the newest waiting record of that axis. An input method composes text in place before it commits it. While a window accepts text (mwinRequestTextInput), mwin_eventImePreedit reports the text being composed, with its caret, its selection and styled segments; an empty preedit ends the composition. Committed text arrives as mwin_eventTextInput. While a composition runs, the keys it consumes produce no key records: a key that types a character and goes down during a composition is left out, and so is its release.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinNextEvent(mwinContext* context, mwinEvent* eventOut);
```
Takes the next record of the stream.  @param context   The context. @param eventOut  Receives the record. @return `mwin_success`; `mwin_empty` when the stream is drained; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

## `gamepad.h`

Gamepads, an optional component (MAUL_WINDOW_GAMEPAD, on by default): without it these functions are not in the library and a call to one fails to link. A gamepad the platform knows the layout of is mapped: its controls come by where they are on a standard pad (the south face button, the left stick), never by the letters printed on them. One it does not know comes raw, as numbered buttons and axes, its hats as pairs of axes. Each connection has its own id; mwin_eventGamepadAdded and mwin_eventGamepadRemoved report hotplug, mwin_eventGamepadChanged a change of its facts (its battery). Its buttons and axes come as records in the stream, whatever window has the focus, and its state can be read at any time. Values are the platform's own, with no dead zone: sticks run from -1 to 1, right and down positive, and triggers from 0 to 1.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetGamepads(const mwinContext* context, mwinGamepadId* gamepads, size_t capacity, size_t* countOut);
```
Lists the gamepads connected now, in the order they came.  @param context   The context. @param gamepads  Receives up to capacity ids. May be NULL when capacity is 0. @param capacity  The ids gamepads holds. @param countOut  Receives the number connected, which may exceed capacity. @return `mwin_success`; `mwin_errorCapacity` when they do not all fit (the first capacity are written); `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetGamepadInfo(const mwinContext* context, mwinGamepadId gamepad, mwinGamepadInfo* infoOut);
```
Reads what the platform tells about a gamepad.  @param context  The context. @param gamepad  The gamepad. @param infoOut  Receives its facts. @return `mwin_success`; `mwin_errorStale` for a gamepad no longer connected; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetGamepadState(const mwinContext* context, mwinGamepadId gamepad, mwinGamepadState* stateOut);
```
Reads a gamepad's controls as the platform last reported them, which may be ahead of the records not yet drained. After a mwin_eventInputStateReset about a gamepad, this is where its state is.  @param context   The context. @param gamepad   The gamepad. @param stateOut  Receives its state. @return As mwinGetGamepadInfo. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinSetGamepadRumble(mwinContext* context, mwinGamepadId gamepad, float low, float high, uint32_t durationMs);
```
Runs a gamepad's motors, the low frequency (heavy) one and the high frequency (light) one, each from 0 (still) to 1, for a time or until the next call: the latest call wins, and a duration of 0 stops them.  @param context     The context. @param gamepad     The gamepad. @param low         The low frequency motor's strength. @param high        The high frequency motor's strength. @param durationMs  How long, in milliseconds. @return `mwin_success`; `mwin_errorUnsupported` for a gamepad without mwin_padRumble; `mwin_errorPlatform` when the platform refused; `mwin_errorStale` for a gamepad no longer connected; `mwin_errorInvalid` for a NULL context or a strength outside 0 to 1. @par Thread safety Main thread only.

## `input.h`

Keyboard, mouse, touch and pen. A key has two names: its code, the physical key by its place on a keyboard (the USB HID usage, so "the key right of Tab" is mwin_codeKeyQ on every layout), and its key, what the current layout makes of it. Text is not keys: typed characters arrive as mwin_eventTextInput, which also carries what an input method composed. The cursor's position arrives as mwin_eventCursorMoved in the window's logical units; relative motion from the device arrives separately as mwin_eventRawPointerDelta, unscaled, and keeps coming while the cursor is captured.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestVirtualKeyboard(mwinContext* context, mwinWindowId window, bool visible, mwinInputPurpose purpose, mwinRequestId* requestOut);
```
Asks to show or hide the on-screen keyboard over a window, where the platform has one; others answer mwin_outcomeUnsupported. mwin_eventVirtualKeyboardChanged reports the part it covers.  @param context     The context. @param window      The window. @param visible     true to show it, false to hide it. @param purpose     What the text field takes. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for an unknown purpose. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestTextInput(mwinContext* context, mwinWindowId window, bool enabled, mwinRect caret, mwinRequestId* requestOut);
```
Asks a window to accept text, or to stop: while it does, input methods compose into it and mwin_eventImePreedit reports their compositions, and the caret rectangle tells the platform where to place the candidate window. Ask again to move the caret.  @param context     The context. @param window      The window. @param enabled     true to accept text, false to stop. @param caret       Where the caret is, in the window's logical units. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for a caret that is not finite or has a negative size. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestCursorMode(mwinContext* context, mwinWindowId window, mwinCursorMode mode, mwinRequestId* requestOut);
```
Asks for a cursor mode over a window.  @param context     The context. @param window      The window. @param mode        One of the mwin_cursor values. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for an unknown mode. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestCursorShape(mwinContext* context, mwinWindowId window, mwinCursorShape shape, mwinRequestId* requestOut);
```
Asks for one of the system's cursor images over a window.  @param context     The context. @param window      The window. @param shape       One of the mwin_shape values. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for an unknown shape. @par Thread safety Main thread only.

```c
mwinKey mwinMapKeyCode(const mwinContext* context, mwinKeyCode code);
```
Returns what a physical key means under the current keyboard layout, as a key record would carry it.  @param context  The context. @param code     A physical key. @return The key; 0 for a NULL context, an unknown code or a key the layout gives no meaning. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetKeyboardLayout(const mwinContext* context, char* buffer, size_t capacity, size_t* lengthOut);
```
Reads the name of the current keyboard layout, as the platform gives it, for showing which layout key labels come from.  @param context    The context. @param buffer     Receives the name in UTF-8, not NUL-terminated. May be NULL when capacity is 0. @param capacity   The bytes buffer holds. @param lengthOut  Receives the name's length in bytes. @return `mwin_success`; `mwin_errorCapacity` when the name does not fit (the bytes that fit are written); `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

## `monitor.h`

Monitors. Each has an id that stays the same while it is connected; mwin_eventMonitorAdded and mwin_eventMonitorRemoved report hotplug, mwin_eventMonitorChanged a change of its facts, and a window whose monitor changes gets mwin_eventDisplayChanged. Listing the monitors gives a consistent snapshot of those connected.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetMonitors(const mwinContext* context, mwinMonitorId* monitors, size_t capacity, size_t* countOut);
```
Lists the monitors connected now.  @param context   The context. @param monitors  Receives up to capacity ids, the primary monitor first. May be NULL when capacity is 0. @param capacity  The ids monitors holds. @param countOut  Receives the number of monitors connected, which may exceed capacity. @return `mwin_success`; `mwin_errorCapacity` when they do not all fit (the first capacity are written); `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetMonitorInfo(const mwinContext* context, mwinMonitorId monitor, mwinMonitorInfo* infoOut);
```
Reads what the platform tells about a monitor.  @param context  The context. @param monitor  The monitor. @param infoOut  Receives the facts. @return `mwin_success`; `mwin_errorStale` for a monitor no longer connected; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

## `native.h`

The native handles a GPU layer needs to create a surface for a window, one bundle per surface generation. This library includes no graphics API header: the handles are the window system's own, as opaque pointers, and the GPU layer turns them into its surface. A lost surface ends a generation; the window's id stays, and the bundle of the next generation arrives with mwin_eventSurfaceRestored.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetNativeHandles(const mwinContext* context, mwinWindowId window, mwinNativeHandles* handlesOut);
```
Reads a window's native handles for its current surface.  @param context     The context. @param window      The window. @param handlesOut  Receives the bundle. @return `mwin_success`; `mwin_errorState` while the window has no surface (before mwin_eventWindowCreated, or between a lost and a restored surface); `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

## `services.h`

What a game or an editor asks of the system around its windows: opening a web or mail address in the user's default program, showing a file in the file manager, keeping the display awake while a window shows, and a message box for an error before any window, or without one. The first three are requests of a window, answered by a completion like any other: the platform may ask the user, or answer later. An address is only http, https or mailto, and holds no spaces or control characters, since a platform may hand it to another program; a path is absolute. Both are copied at the call.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestOpenUrl(mwinContext* context, mwinWindowId window, const char* url, size_t length, mwinRequestId* requestOut);
```
Asks the system to open a web or mail address in the user's default program.  @param context    The context. @param window     The window asking. @param url        UTF-8, not NUL-terminated: http://, https:// or mailto: (in any case), then no spaces or control characters. @param length     Its bytes, at most MWIN_ADDRESS_BYTES. @param requestOut Receives the request's id. May be NULL. @return `mwin_success`; `mwin_errorCapacity` when the context cannot hold the copy or the window has its limit of requests in flight; `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL context or an address this does not open. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestRevealFile(mwinContext* context, mwinWindowId window, const char* path, size_t length, mwinRequestId* requestOut);
```
Asks the file manager to show a file, selected where it can.  @param context    The context. @param window     The window asking. @param path       An absolute path in UTF-8, not NUL-terminated, without NULs. @param length     Its bytes, at most MWIN_ADDRESS_BYTES. @param requestOut Receives the request's id. May be NULL. @return As mwinRequestOpenUrl; `mwin_errorInvalid` also for a path that is not absolute. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestKeepAwake(mwinContext* context, mwinWindowId window, bool awake, mwinRequestId* requestOut);
```
Asks the system to keep the display awake, with no screensaver or dimming, while the window shows; or no longer. The window's state says whether it does (mwinWindowState's awake).  @param context    The context. @param window     The window. @param awake      true to keep the display awake. @param requestOut Receives the request's id. May be NULL. @return `mwin_success`; `mwin_errorCapacity` when the window has its limit of requests in flight; `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL context. @par Thread safety Main thread only.

```c
mwinMessageBoxDef mwinDefaultMessageBoxDef(void);
```
Returns the default message box def: no title or message, an error with an OK button.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinShowMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut);
```
Shows a message box and waits for the user. It needs no context, so a program can report an error that stops it from starting. On Linux it runs zenity or kdialog, whichever is there. On iOS it needs a scene in the foreground, which the application does not show yet in the program's init: there it fails.  @param def          The message box. @param acceptedOut  Receives true for OK or Yes, false for Cancel, No or a closed box. May be NULL. @return `mwin_success`; `mwin_errorUnsupported` where the platform has no message box (Linux without zenity or kdialog, the test backend alone); `mwin_errorPlatform` when it failed; `mwin_errorInvalid` for a NULL or invalid def, or text that is not UTF-8, holds a NUL or passes its limit. @par Thread safety Main thread only.

## `system.h`

What the system says about the user's preferences and the machine: the theme and accent color, reduced motion and text scale, the power source, and the preferred locales. Each has a notification when it changes; the values are read at any time.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetSystemFacts(const mwinContext* context, mwinSystemFacts* factsOut);
```
Reads the system's preferences and facts.  @param context   The context. @param factsOut  Receives them. @return `mwin_success`; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetPreferredLocales(const mwinContext* context, char* buffer, size_t capacity, size_t* lengthOut);
```
Reads the user's preferred locales, most preferred first, as BCP 47 tags separated by commas ("de-DE,en-GB"); empty where the platform does not say.  @param context    The context. @param buffer     Receives the list, not NUL-terminated. May be NULL when capacity is 0. @param capacity   The bytes buffer holds. @param lengthOut  Receives the list's length in bytes. @return `mwin_success`; `mwin_errorCapacity` when the list does not fit (the bytes that fit are written); `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

## `test.h`

The test backend, a headless platform for contract tests. It exists only in builds with MAUL_WINDOW_TEST_BACKEND and only in a context created with mwin_backendTest; it is never a fallback. It answers each request at the next pump, as the answer set for its kind says, and takes what a platform would report from mwinTestPost, which sends it through the same path a real backend's reports take.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestSetAnswer(mwinContext* context, mwinRequestKind kind, mwinOutcome outcome);
```
Sets how the test platform answers requests of a kind from now on. mwin_outcomeDone, the default, carries a request out at the next pump.  @param context  A context of the test backend. @param kind     The kind of request. @param outcome  The answer: any mwin_outcome value but superseded and cancelled, which only the core gives; cancelled too for file dialogs, as a user closing one. @return `mwin_success`; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context, an unknown kind or an answer the platform cannot give. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestHold(mwinContext* context, bool hold);
```
Holds requests: while held, none is answered.  @param context  A context of the test backend. @param hold     true to hold, false to answer again from the next pump. @return `mwin_success`; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestPost(mwinContext* context, const mwinEvent* event);
```
Reports what the platform would: the user resized or closed a window, focus moved, the scale changed, a key went down, text was typed, the application is suspending. The report reaches the stream at the next pump, stamped with the time of this call; text is copied from the record's pointer. The pump runs a frame at once after a lifecycle or surface report, as a platform that waits for the program would make it; a lifecycle report needs no window. Completions, input state resets and the created and destroyed records are the core's and refused.  @param context  A context of the test backend. @param event    The report; its window must be live, but for the lifecycle. @return `mwin_success`; `mwin_errorStale` for a window that no longer exists; `mwin_errorCapacity` when 1,024 reports or 64 KiB of their text already wait; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument, a record type only the core makes, or text that is not UTF-8. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestSetTime(mwinContext* context, uint64_t timeNs);
```
Sets the test platform's clock, which stamps every record.  @param context  A context of the test backend. @param timeNs   Nanoseconds; it may not go back. @return `mwin_success`; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context or a time before the current one. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestSetScale(mwinContext* context, float scale);
```
Sets the scale the test platform gives windows it creates or resizes from now on; 1 at first.  @param context  A context of the test backend. @param scale    A positive, finite scale. @return `mwin_success`; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context or a scale that is not positive and finite. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestGetTitle(const mwinContext* context, mwinWindowId window, char* buffer, size_t capacity, size_t* lengthOut);
```
Reads the title the test platform shows for a window.  @param context    A context of the test backend. @param window     The window. @param buffer     Receives the title, not NUL-terminated. May be NULL when capacity is 0. @param capacity   The bytes buffer holds. @param lengthOut  Receives the title's length in bytes. @return `mwin_success`; `mwin_errorCapacity` when the title does not fit (the bytes that fit are written); `mwin_errorStale` for a window that no longer exists; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestAddMonitor(mwinContext* context, const mwinMonitorInfo* info, mwinMonitorId* monitorOut);
```
Connects a monitor to the test platform, at once: its mwin_eventMonitorAdded waits in the stream when this returns. Windows the platform makes from now on show on the primary monitor.  @param context     A context of the test backend. @param info        The monitor's facts. @param monitorOut  Receives its id. @return `mwin_success`; `mwin_errorCapacity` when the context has its limit of monitors; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestChangeMonitor(mwinContext* context, mwinMonitorId monitor, const mwinMonitorInfo* info);
```
Changes a connected monitor's facts, at once.  @param context  A context of the test backend. @param monitor  The monitor. @param info     Its new facts. @return `mwin_success`; `mwin_errorStale` for a monitor no longer connected; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestRemoveMonitor(mwinContext* context, mwinMonitorId monitor);
```
Disconnects a monitor, at once.  @param context  A context of the test backend. @param monitor  The monitor. @return `mwin_success`; `mwin_errorStale` for a monitor no longer connected; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestSetSystemFacts(mwinContext* context, const mwinSystemFacts* facts);
```
Sets the system's preferences and facts, at once; a change of the look posts mwin_eventThemeChanged, of the power mwin_eventPowerChanged.  @param context  A context of the test backend. @param facts    The facts. @return `mwin_success`; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestSetLocales(mwinContext* context, const char* locales, size_t length);
```
Sets the user's preferred locales, at once; a change posts mwin_eventLocaleChanged.  @param context  A context of the test backend. @param locales  BCP 47 tags separated by commas, UTF-8. May be NULL when length is 0. @param length   The number of bytes. @return `mwin_success`; `mwin_errorCapacity` past the localeBytes limit; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context or a list that is not UTF-8. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestAddGamepad(mwinContext* context, const mwinGamepadInfo* info, mwinGamepadId* gamepadOut);
```
Connects a gamepad to the test platform, at once: its mwin_eventGamepadAdded waits in the stream when this returns.  @param context     A context of the test backend. @param info        The gamepad's facts. @param gamepadOut  Receives its id. @return `mwin_success`; `mwin_errorCapacity` when the context has its limit of gamepads; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestChangeGamepad(mwinContext* context, mwinGamepadId gamepad, const mwinGamepadInfo* info);
```
Changes a connected gamepad's facts, at once.  @param context  A context of the test backend. @param gamepad  The gamepad. @param info     Its new facts. @return `mwin_success`; `mwin_errorStale` for a gamepad no longer connected; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestRemoveGamepad(mwinContext* context, mwinGamepadId gamepad);
```
Disconnects a gamepad, at once.  @param context  A context of the test backend. @param gamepad  The gamepad. @return As mwinTestChangeGamepad, with `mwin_errorInvalid` for a NULL context. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestGamepadButton(mwinContext* context, mwinGamepadId gamepad, uint8_t button, bool down);
```
Presses or lets go a gamepad's button, at once; a button already so posts nothing.  @param context  A context of the test backend. @param gamepad  The gamepad. @param button   An mwinGamepadButton, or a raw button's number. @param down     true to press it. @return As mwinTestRemoveGamepad. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestGamepadAxis(mwinContext* context, mwinGamepadId gamepad, uint8_t axis, float value);
```
Moves a gamepad's axis, at once; a value it has already posts nothing.  @param context  A context of the test backend. @param gamepad  The gamepad. @param axis     An mwinGamepadAxis, or a raw axis's number. @param value    Where it is. @return As mwinTestRemoveGamepad. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestGetRumble(const mwinContext* context, mwinGamepadId gamepad, float* lowOut, float* highOut, uint32_t* durationMsOut, uint32_t* countOut);
```
Reads the last rumble a gamepad of the test platform was given.  @param context        A context of the test backend. @param gamepad        The gamepad. @param lowOut         Receives the low frequency motor's strength. @param highOut        Receives the high frequency motor's strength. @param durationMsOut  Receives the duration. @param countOut       Receives how many rumbles it was given. @return As mwinTestChangeGamepad. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestSetClipboard(mwinContext* context, const char* bytes, size_t length);
```
Puts bytes on the test platform's clipboard, at once, as another program would: they need not be UTF-8.  @param context  A context of the test backend. @param bytes    The bytes. May be NULL when length is 0. @param length   Their number. @return `mwin_success`; `mwin_errorCapacity` when the allocator has no room; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context, or NULL bytes with a length. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestSetClipboardUtf16(mwinContext* context, const uint16_t* units, size_t length);
```
Puts UTF-16 on the test platform's clipboard, as Windows keeps it: it need not be well-formed.  @param context  A context of the test backend. @param units    The code units. May be NULL when length is 0. @param length   Their number. @return As mwinTestSetClipboard. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestGetClipboard(const mwinContext* context, char* buffer, size_t capacity, size_t* lengthOut);
```
Reads the test platform's clipboard as bytes: what the program last wrote, or what mwinTestSetClipboard put there.  @param context    A context of the test backend. @param buffer     Receives the bytes. May be NULL when capacity is 0. @param capacity   The bytes buffer holds. @param lengthOut  Receives their number. @return `mwin_success`; `mwin_errorCapacity` when they do not fit (the bytes that fit are written); `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestDrop(mwinContext* context, mwinWindowId window, mwinPosition position, const char* files, size_t filesLength, const char* text, size_t textLength);
```
Drops files and text on a window of the test platform: gathered at once, delivered in order with the reports at the next pump. The drag's own records are reported with mwinTestPost.  @param context      A context of the test backend. @param window       The window. @param position     Where, in the window. @param files        The paths as the platform gives them, each ended by a NUL; they need not be UTF-8. May be NULL when filesLength is 0. @param filesLength  Their bytes, NULs included. @param text         The text, which need not be UTF-8, or NULL for none. @param textLength   Its bytes. @return `mwin_success`; `mwin_errorState` while an earlier drop waits; `mwin_errorCapacity` when 1,024 reports wait; `mwin_errorStale` for a window that no longer exists; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context, or files that are NULL with a length or not ended by a NUL. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestGetOpened(const mwinContext* context, mwinRequestKind kind, char* buffer, size_t capacity, size_t* lengthOut);
```
Reads the last address the test platform opened (mwin_requestOpenUrl) or path it revealed (mwin_requestRevealFile).  @param context    A context of the test backend. @param kind       mwin_requestOpenUrl or mwin_requestRevealFile. @param buffer     Receives it. May be NULL when capacity is 0. @param capacity   The bytes buffer holds. @param lengthOut  Receives its length in bytes, 0 before any. @return `mwin_success`; `mwin_errorCapacity` when it does not fit (the bytes that fit are written); `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument or another kind. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestSetDialogFiles(mwinContext* context, const char* files, size_t length);
```
Sets the paths the next file dialogs choose when they are done: each ended by a NUL. None makes a done dialog fail.  @param context A context of the test backend. @param files   The paths. May be NULL when length is 0. @param length  Their bytes, at most 1024. @return `mwin_success`; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context, or paths too long or not ended by a NUL. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestGetDialog(const mwinContext* context, char* buffer, size_t capacity, size_t* lengthOut);
```
Copies out the last file dialog the backend was asked for, as lines: its kind as a digit, title, folder and name, then each filter as its name, ':' and its extensions.  @param context   A context of the test backend. @param buffer    Receives it. May be NULL when capacity is 0. @param capacity  The bytes buffer holds. @param lengthOut Receives its length in bytes, 0 before any. @return As mwinTestGetOpened. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestGetIcon(const mwinContext* context, uint32_t* countOut, uint64_t* checksumOut);
```
Reads the icon the backend was last given: how many images, and a checksum of them in order, 64-bit FNV-1a over each image's width and height (4 bytes each, least significant first) and its pixels, packed.  @param context     A context of the test backend. @param countOut    Receives the images, 0 before any icon. @param checksumOut Receives the checksum. @return `mwin_success`; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinTestAskAccessibility(mwinContext* context, mwinWindowId window);
```
Plays an accessibility client asking the window for its tree, as a screen reader would: the first time, mwin_eventAccessibilityRequested follows.  @param context A context of the test backend. @param window  The window. @return `mwin_success`; `mwin_errorStale` for a window that no longer exists; `mwin_errorUnsupported` for a context of another backend; `mwin_errorInvalid` for a NULL context. @par Thread safety Main thread only.

## `window.h`

Windows. A window is created at once as an id; the platform makes it real later and says so with mwin_eventWindowCreated. Every change to a window is a request (family record 0018): the call returns a request id, and exactly one mwin_eventRequestCompleted answers it, after the notifications the change caused. A later request of the same kind on the same window supersedes an earlier one still in flight. Nothing closes a window by itself. The platform's close button sends mwin_eventCloseRequested; the program destroys the window, or does not. Sizes come in three kinds: the logical size (the unit the program lays out in), the size in pixels, and the scale between them. Each has its own notification.

```c
mwinWindowDef mwinDefaultWindowDef(void);
```
Returns the default window def: 1,280 by 720 logical units, windowed, visible, resizable and decorated, not always on top, with no title.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinCreateWindow(mwinContext* context, const mwinWindowDef* def, mwinWindowId* windowOut, mwinRequestId* requestOut);
```
Creates a window. Its id is valid at once; mwin_eventWindowCreated follows when the platform has made it, then the completion of the request.  @param context    The context. @param def        The window: a valid cookie, a positive size, a UTF-8 title within the titleBytes limit, a UTF-8 canvas selector within MWIN_CANVAS_SELECTOR_BYTES; a popup windowed, with an owner and a finite position. @param windowOut  Receives the window's id. @param requestOut Receives the id of the creation request. May be NULL. @return `mwin_success`; `mwin_errorCapacity` when the context has its limit of windows; `mwin_errorStale` for an owner that no longer exists; `mwin_errorInvalid` for a NULL argument or an invalid def. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinDestroyWindow(mwinContext* context, mwinWindowId window);
```
Destroys a window at once: its id becomes stale, its requests in flight complete as cancelled, and mwin_eventWindowDestroyed follows. Notifications of the window not yet drained are dropped. The windows it owns are destroyed first, theirs before them.  @param context  The context. @param window   The window. @return `mwin_success`; `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL context. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinGetWindowState(const mwinContext* context, mwinWindowId window, mwinWindowState* stateOut);
```
Reads what the program has been told about a window.  @param context   The context. @param window    The window. @param stateOut  Receives the state. @return `mwin_success`; `mwin_errorStale` for a window that no longer exists; `mwin_errorInvalid` for a NULL argument. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestTitle(mwinContext* context, mwinWindowId window, const char* title, size_t length, mwinRequestId* requestOut);
```
Asks for a new title.  @param context     The context. @param window      The window. @param title       UTF-8. May be NULL when length is 0. @param length      The number of bytes, at most the titleBytes limit. @param requestOut  Receives the request's id. May be NULL. @return `mwin_success`; `mwin_errorStale` for a window that no longer exists; `mwin_errorCapacity` when the window has its limit of requests in flight or the title is too long; `mwin_errorInvalid` for a NULL context or a title that is not UTF-8. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestSize(mwinContext* context, mwinWindowId window, mwinSize size, mwinRequestId* requestOut);
```
Asks for a new logical size of the area the program draws in. mwin_eventResized and mwin_eventPixelSizeChanged report what the platform chose, which may differ.  @param context     The context. @param window      The window. @param size        A positive, finite size. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for a size that is not positive and finite. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestPosition(mwinContext* context, mwinWindowId window, mwinPosition position, mwinRequestId* requestOut);
```
Asks to move a window. Wayland does not let programs place windows, and answers mwin_outcomeUnsupported, except popups. A popup's position is from the top left of its owner's client area.  @param context     The context. @param window      The window. @param position    A finite position. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for a position that is not finite. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestMode(mwinContext* context, mwinWindowId window, mwinWindowMode mode, mwinRequestId* requestOut);
```
Asks for a mode: windowed, borderless fullscreen, minimized or maximized. A popup, always windowed, answers mwin_outcomeUnsupported.  @param context     The context. @param window      The window. @param mode        One of the mwin_mode values. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for an unknown mode. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestVisible(mwinContext* context, mwinWindowId window, bool visible, mwinRequestId* requestOut);
```
Asks to show or hide a window.  @param context     The context. @param window      The window. @param visible     true to show it, false to hide it. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestSizeLimits(mwinContext* context, mwinWindowId window, mwinSize minimum, mwinSize maximum, mwinRequestId* requestOut);
```
Asks for limits on the logical size the user can give the window; a zero width or height leaves that side free.  @param context     The context. @param window      The window. @param minimum     The smallest size, zero or positive and finite. @param maximum     The largest size, zero or at least minimum. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for limits that are not finite, negative, or crossed. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestAspectRatio(mwinContext* context, mwinWindowId window, uint32_t width, uint32_t height, mwinRequestId* requestOut);
```
Asks the platform to keep the window's width to height at a ratio while the user resizes it; 0 by 0 lifts the constraint.  @param context     The context. @param window      The window. @param width       The ratio's width. @param height      The ratio's height; both zero or both positive. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for a ratio with one side zero. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestStyle(mwinContext* context, mwinWindowId window, mwinWindowStyle style, mwinRequestId* requestOut);
```
Asks for a style: resizable, decorated, always on top. A popup, undecorated and above its owner, answers mwin_outcomeUnsupported.  @param context     The context. @param window      The window. @param style       mwin_style flags. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for unknown flags. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestOpacity(mwinContext* context, mwinWindowId window, float opacity, mwinRequestId* requestOut);
```
Asks for the window's opacity, where the platform can blend windows; others answer mwin_outcomeUnsupported.  @param context     The context. @param window      The window. @param opacity     From 0 to 1. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for an opacity outside 0 to 1. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestHitRegions(mwinContext* context, mwinWindowId window, const mwinHitRegion* regions, uint32_t count, mwinRequestId* requestOut);
```
Tells the platform what the parts of the window's client area are, replacing what it was told before: a later region over an earlier one, and the client everywhere else. A press on a caption or an edge moves or resizes the window through the platform, and is not reported. The regions are copied at the call. The web answers mwin_outcomeUnsupported.  @param context     The context. @param window      The window. @param regions     The regions. May be NULL when count is 0. @param count       How many, at most MWIN_HIT_REGIONS; 0 for none. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorInvalid` for more regions than MWIN_HIT_REGIONS, a region not finite, of a negative size or of an unknown kind. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestIcon(mwinContext* context, mwinWindowId window, const mwinIconImage* images, uint32_t count, mwinRequestId* requestOut);
```
Asks for the window's icon, as its title bar, the taskbar and the window switcher show it; each platform takes the images nearest the sizes it shows. The images are copied at the call. The web, whose page has one icon for every canvas, and Wayland compositors without toplevel icons answer mwin_outcomeUnsupported.  @param context     The context. @param window      The window. @param images      The images, in any order. May be NULL when count is 0. @param count       How many, at most MWIN_ICON_IMAGES; 0 for the platform's own icon. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle, with `mwin_errorCapacity` when the context cannot hold the copy, and `mwin_errorInvalid` for more images than MWIN_ICON_IMAGES, an image without pixels, with no width or height or more than MWIN_ICON_SIZE, or a stride shorter than its row. @par Thread safety Main thread only.

```c
MWIN_NODISCARD MWIN_API mwinResult mwinRequestFocus(mwinContext* context, mwinWindowId window, mwinRequestId* requestOut);
```
Asks for keyboard focus. Platforms may refuse to take focus from another program, and answer mwin_outcomeDenied.  @param context     The context. @param window      The window. @param requestOut  Receives the request's id. May be NULL. @return As mwinRequestTitle. @par Thread safety Main thread only.

---

78 functions across 15 headers.
