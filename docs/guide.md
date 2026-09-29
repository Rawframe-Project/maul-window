# The Maul Window guide

This guide walks through Maul Window part by part: how a program runs,
windows and their requests, input, gamepads, the platform's services,
and how to test a program without a window system. The
[API reference](api.md) lists every public function; the design
records in [adr/](adr/mwin.md) give the reasons.

## 1. The model

A program describes itself in an application def and hands it to
`mwinRun`:

```c
#include "maul-window/event.h"
#include "maul-window/window.h"

static mwinWindowId s_window;

static mwinResult Init(mwinContext* context, void* user)
{
    (void)user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = "Hello";
    def.titleLength = 5;
    def.size = (mwinSize){800.0f, 600.0f};
    return mwinCreateWindow(context, &def, &s_window, NULL);
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    (void)user;
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        if (event.type == mwin_eventCloseRequested)
        {
            return mwin_frameStop;
        }
    }
    // Draw the frame here.
    return mwin_frameContinue;
}

int main(void)
{
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    return mwinRun(&def) == mwin_success ? 0 : 1;
}
```

`mwinRun` creates the context, calls `init` once, calls `frame` once per
frame until it returns `mwin_frameStop`, calls `quit` (optional), and
destroys the context. On Win32, Wayland and X11 it pumps the platform
itself; on the web the browser's loop drives it and `mwinRun` may never
return, so cleanup belongs in `quit`. The program cannot tell the two
apart.

Three rules hold everywhere:

- **One stream.** Everything the platform says arrives in one ordered
  stream of records, across windows, which `frame` drains with
  `mwinNextEvent`. Records wait in storage per window, so one busy
  window cannot push out another's.
- **Requests and completions.** Every change to a window is a request.
  The call returns an `mwinRequestId` at once, and exactly one
  `mwin_eventRequestCompleted` answers it later, after the
  notifications the change caused. The outcome is done, or one reason
  from a closed list: unsupported, denied, superseded (a later request
  of the same kind replaced it), cancelled (its window went first),
  failed, too large.
- **No threads, no callbacks.** The library starts no thread and never
  calls the program except through `init`, `frame` and `quit`.

Every limit on memory and work is a field of `mwinLimits` in the
context def, with a documented default; a request past one is refused
with `mwin_errorCapacity`. All the context's memory is taken when it is
created, through the def's allocator.

## 2. Results and ids

Functions return an `mwinResult`: `mwin_success`, or an error
(`mwin_errorInvalid` for a bad argument, `mwin_errorStale` for an id
whose object is gone, `mwin_errorCapacity`, `mwin_errorUnsupported`,
`mwin_errorPlatform`). `mwinResultName` names each. Windows, monitors,
gamepads and requests are ids with a generation: an id outlives its
object harmlessly, and using it afterwards answers `mwin_errorStale`.

Defs (`mwinWindowDef`, `mwinFileDialogDef`, `mwinMessageBoxDef`) carry a
cookie. Build them with their `mwinDefault...` function and change
fields; a def made any other way is refused.

## 3. Windows

`mwinCreateWindow` returns the window's id at once; the platform makes
it real later and says so with `mwin_eventWindowCreated`, and the
request completes. The def gives the title, the logical size, the mode
(windowed, borderless fullscreen, minimized, maximized), visibility and
style bits (resizable, decorated, always on top, custom chrome).

Sizes come in three kinds, each with its own notification: the logical
size the program lays out in (`mwin_eventResized`), the size in pixels
it renders at (`mwin_eventPixelSizeChanged`), and the scale between
them (`mwin_eventScaleChanged`, with the size the platform suggests).
`mwinGetWindowState` gives what the notifications delivered so far.

The requests are `mwinRequestTitle`, `Size`, `Position`, `Mode`,
`Visible`, `Focus`, `SizeLimits`, `AspectRatio`, `Style`, `Opacity`
and `Icon`. What a platform cannot do it answers unsupported: Wayland
places no toplevel, and the web has no title bar. Nothing closes a
window by itself. The close button sends `mwin_eventCloseRequested`, and
the program calls `mwinDestroyWindow`, or does not.

### Editor windows

- **Owned windows:** a def's `owner` makes an owned window, which stays
  above its owner and goes with it.
- **Popups:** `kind` makes a menu or a tooltip popup, placed at
  `position` from its owner's client area and moved with it. A menu
  that loses the keyboard asks to close.
- **Custom chrome:** a window with `mwin_styleCustomChrome` draws its own
  title bar. `mwinRequestHitRegions` tells the platform which rectangles
  are caption, edges, corners and buttons. Moving and resizing then
  behave as the platform's own do, and Windows 11 shows its snap layouts
  over the maximize region.

## 4. Monitors and system facts

`mwinGetMonitors` and `mwinGetMonitorInfo` describe the connected
monitors: their bounds and work area in pixels, physical size, scale,
refresh rate, whether each is the primary one, and HDR facts. Hotplug and
changes arrive as `mwin_eventMonitorAdded`, `Removed` and `Changed`.

`mwinGetSystemFacts` gives the user's theme, accent color, reduced
motion, text scale, power source and power saving, and whether Windows
shows snap layouts. Changes arrive as `mwin_eventThemeChanged` and
`mwin_eventPowerChanged`. `mwinGetPreferredLocales` gives the preferred
languages as BCP 47 tags (`de-DE,en-GB`), with `mwin_eventLocaleChanged`.
On Linux they come from the desktop portal, UPower and the environment,
so they may be unknown for the first frames.

## 5. Input

Input is per window, in four classes with their own storage: discrete
records (keys, text, buttons, touches and pen contacts), motion, raw
deltas, and the wheel.

- **Discrete records** are never merged. When their storage is full,
  the window gets `mwin_eventInputStateReset` instead. The same record
  follows every loss of focus, after which no key or button counts as
  held.
- **Motion, deltas and wheel** merge into the newest waiting record when
  storage is full, and `samples` says how many a record stands for.

**Keys.** A key has two names:

- its code, the physical key by its USB HID usage (`mwin_codeKeyQ` is
  the key right of Tab on every layout);
- its key, what the layout makes of it (`mwinMapKeyCode`).

Typed text is not keys. It arrives as `mwin_eventTextInput`, while the
window accepts text (`mwinRequestTextInput`, with the caret's rectangle
for the input method's window). An input method's composition arrives
as `mwin_eventImePreedit`, with its caret, selection and styled
segments. The keys it consumes produce no key records.
`mwinRequestVirtualKeyboard` shows an on-screen keyboard where there is
one.

**The pointer.** `mwin_eventCursorMoved` is in the window's logical
units; buttons carry the count of quick clicks they complete. The wheel
turns in detents, fractional for touchpads and smooth wheels.
`mwin_eventRawPointerDelta` is the device's own motion. It keeps coming
while `mwinRequestCursorMode` captures the cursor, as a first-person
camera needs. `mwinRequestCursorShape` picks the system's cursors.

**Touch and pen.** These are records of their own, with ids,
pressure, tilt and the pen's buttons.

## 6. Gamepads

Gamepads belong to the context, not a window. They are an optional
component (`MAUL_WINDOW_GAMEPAD`, on by default).

- **Hotplug:** `mwin_eventGamepadAdded` and `Removed`.
- **Controls:** `mwin_eventGamepadButtonDown`, `Up` and
  `mwin_eventGamepadAxisMoved`. `mwinGetGamepadState` reads them as the
  platform last reported them, for a program that polls.

A gamepad the platform or SDL_GameControllerDB knows is **mapped**
onto a standard layout named by place:

- the d-pad;
- four face buttons by compass point;
- the shoulders, the stick clicks, start, select and guide;
- two sticks from -1 to 1, with right and down positive;
- two triggers from 0 to 1.

Any other is **raw**: numbered buttons and axes, with each hat as two
axes. Nothing is filtered, so dead zones are the program's.
`mwinSetGamepadRumble` drives the motors where
`mwinGetGamepadInfo` says there are some; the latest call wins.

On Linux gamepads are evdev devices. On Windows, Xbox pads come through
Windows.Gaming.Input, any number of them with their names and
batteries, or through XInput's four players where the runtime is
missing; the runtime may give a program in the background no input.
Other pads come through Raw Input and the HID parser. On
the web they come through the Gamepad API.

## 7. Clipboard, drag and drop

The clipboard holds UTF-8 text. `mwinRequestClipboardWrite` and
`mwinRequestClipboardRead` are requests of a window. The browser may
ask the user first, and on Wayland only a focused window may use it. A
read that completes done leaves its text for `mwinGetClipboardText`.
Text from other programs has ill-formed UTF-8 replaced with U+FFFD, and
text past `clipboardBytes` completes the read too large.

While something is dragged over a window, the program hears
`mwin_eventDragEntered`, `DragMoved` and `DragLeft`; a drop brings
`mwin_eventDropped` instead of the leaving. Its files, as UTF-8 paths
each ended by a NUL, and its text wait under the drop's number for
`mwinGetDroppedFiles` and `mwinGetDroppedText` until the next drop.

## 8. Platform services

These are requests of a window, answered like any other:

- `mwinRequestFileDialog` opens the platform's file dialog: open, open
  several, save, or choose a folder, with filters by extension. After a
  done completion, `mwinGetDialogFiles` copies out the paths. It uses
  the common item dialog on Windows, and the desktop portal (else
  zenity) on Linux. The web has none.
- `mwinRequestOpenUrl` opens an http, https or mailto address in the
  user's program for it.
- `mwinRequestRevealFile` shows a file in the file manager.
- `mwinRequestKeepAwake` keeps the display on while the window shows.

`mwinShowMessageBox` needs no context. It shows a platform message box
and waits, for errors a program must report before or after it has
windows.

## 9. Accessibility hooks

The accessibility tree is Maul UI's; the window only hands it to the
platform:

- On Windows, `mwinRequestAccessibilityRoot` gives UI Automation the
  program's root provider.
- On the web, each window has a host element over its canvas for the
  program's ARIA elements. Its selector is in the native handles.
- `mwin_eventAccessibilityRequested` comes the first time a client asks
  a window for its tree. A program can build its tree only then.

## 10. Handing a window to a GPU layer

`mwinGetNativeHandles` gives the window system's own handles for a
window, as opaque pointers:

- Win32: the `HWND` and `HINSTANCE`;
- Wayland: the `wl_display` and `wl_surface`;
- X11: the XCB connection and window;
- the web: the canvas's selector.

A GPU layer (Maul RHI) makes its surface from them. The library
includes no graphics API header. A lost surface
(`mwin_eventSurfaceLost`) ends a generation of handles, and the next
arrives with `mwin_eventSurfaceRestored`.

## 11. Backends and building

| Backend | Platform API | Built when |
| --- | --- | --- |
| Win32 | user32, Raw Input, IMM32, OLE drag and drop, Windows.Gaming.Input, XInput | Windows |
| Wayland | xdg-shell and its extensions, the desktop portal, evdev | Linux (`MAUL_WINDOW_WAYLAND`) |
| X11 | XCB, XInput 2.1, XKB, XDND, the desktop portal, evdev | Linux (`MAUL_WINDOW_X11`) |
| Web | an HTML canvas and the browser's APIs | Emscripten, or wasm32-wasi |

On Linux one build carries both. At run time Wayland is chosen where
`WAYLAND_DISPLAY` names a compositor, else X11. The Linux system
libraries (libwayland, libxcb, libxkbcommon, libdbus) are loaded when a
context starts, so a program runs where they are missing, without that
backend. The build options are in the [README](../README.md).

On the web without Emscripten (a wasm32-wasi build), the backend's
JavaScript comes as imports. The build writes `maul-window.mjs` beside
the library (its target property `MAUL_WINDOW_WEB_GLUE` names it); the
page loads it, puts what `maulWindowImports(() => instance.exports)`
returns among the instance's `env` imports, and the program links with
`-Wl,--export-table`. There `mwinRun` returns `mwin_success` once init
has succeeded, and the page's animation frames run the program on;
quit still ends it.

## 12. Testing a program

A context def with `backend = mwin_backendTest` runs on a headless test
backend, in builds with `MAUL_WINDOW_TEST_BACKEND`. It is never a
fallback.

- It answers each request at the next pump. `mwinTestSetAnswer` sets
  the outcome for a kind.
- It takes what a platform would report from the `mwinTest` functions
  of `maul-window/test.h`. Records such as monitors, gamepads, drops,
  the clipboard, a scale change and system facts go through the path a
  real backend's reports take.

A program's logic can be tested the same way on every machine. The
library's own contract tests run on this backend, and each native
backend has its own tests: on Xvfb, a compositor of the test's own,
Wine or Windows, and headless Chrome.

The parsers of bytes that other programs and devices hand the library
have libFuzzer targets (`-DMAUL_WINDOW_FUZZ=ON`, with Clang), which CI
runs on every push.
