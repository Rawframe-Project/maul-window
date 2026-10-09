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
itself. On macOS it runs AppKit's loop, which calls the frames from the
screen's display link, and returns once a frame stops. On iOS it runs
UIKit's, which never returns: init runs when the application's first
scene connects, frames follow the display while a scene shows, and a
program that stops ends with `quit` while the application runs on. On
Android the platform begins the run: the program defines
`mwinAndroidMain` in place of `main`, returning the same def, and the
library's activity runs it on the main thread, frames following the
display; an activity the system makes anew (a rotation, the user coming
back) joins the running program. On the web the browser's loop drives
it and `mwinRun` may never return, so cleanup belongs in `quit`. The
program cannot tell these apart.

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
Each `mwin_errorInvalid` a live context returns also counts one
misuse, which `mwinGetContextMisuse` reads: a release build can watch
it to catch a program's bugs, while stale ids count nothing.

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
places no toplevel, and the web has no title bar. A macOS window has no
icon of its own, so its icon request sets the application's. An iOS
window is a scene's, which the system sizes and places; its title is
what the app switcher shows, and a program on an iPhone has one window.
An Android program has one window too, its activity's, drawn edge to
edge behind the system's bars, whose safe area the window state gives.
Nothing closes a window by itself. The close button sends `mwin_eventCloseRequested`, and
the program calls `mwinDestroyWindow`, or does not. On Android the Back
key and gesture send it too, so a program goes back a screen or ends.

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

The HDR facts say what each platform tells (mwin-0036): Win32, Wayland
(with the color manager) and Android give luminances in nits; X11 the
EDID's, with HDR output never on; macOS and iOS the headroom alone; the
web only whether the screen shows HDR. `headroom`, the peak over SDR
white, is there wherever it can be known. Variable refresh is told on
X11 (`vrr_capable`), macOS, iOS and Android 16, and false elsewhere.

`mwinGetSystemFacts` gives the user's theme, accent color, reduced
motion, text scale, power source and power saving, and whether Windows
shows snap layouts. Changes arrive as `mwin_eventThemeChanged` and
`mwin_eventPowerChanged`. `mwinGetPreferredLocales` gives the preferred
languages as BCP 47 tags (`de-DE,en-GB`), with `mwin_eventLocaleChanged`.
On Linux they come from the desktop portal, UPower and the environment,
so they may be unknown for the first frames. On the web the power
source comes from the Battery Status API where the browser has it
(Chromium), once it answers; power saving is never known there.

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

Some chords never reach a program: Ctrl+W in Chromium, Alt+Tab on
Windows, Command+Tab on Apple's systems. `mwinGetKeyReach` tells, for
a key with modifiers, whether it is delivered, shared with the
platform (a browser shortcut the page also gets), uncertain (the
desktop's configurable shortcuts may take it) or never delivered, so
a rebinding UI can refuse or warn (mwin-0031).

Typed text is not keys. It arrives as `mwin_eventTextInput`, while the
window accepts text (`mwinRequestTextInput`, with the caret's rectangle
for the input method's window). An input method's composition arrives
as `mwin_eventImePreedit`, with its caret, selection and styled
segments. The keys it consumes produce no key records. On X11 the
input method is IBus or Fcitx 5, reached over the session bus
(mwin-0030); without either, text comes from the keymap.
`mwinRequestVirtualKeyboard` shows an on-screen keyboard where there is
one.

**The pointer.** `mwin_eventCursorMoved` is in the window's logical
units; buttons carry the count of quick clicks they complete. The wheel
turns in detents, fractional for touchpads and smooth wheels.
`mwin_eventRawPointerDelta` is the device's own motion. It keeps coming
while `mwinRequestCursorMode` captures the cursor, as a first-person
camera needs. `mwinRequestCursorShape` picks the system's cursors;
`mwinCreateCursor` makes one from images, the first at scale 1 and the
others for higher scales, and `mwinRequestCursorImage` shows it over a
window, which takes the image for its scale.

**Touch and pen.** These are records of their own, with ids,
pressure, tilt and the pen's buttons. A touch screen's touches make
touch records and no mouse record: on X11 through XInput 2.2 where the
server has it (mwin-0039), where no touch is ever cancelled, since X11
tells a program none. A pen makes pen records and no mouse record on
every platform: Wayland through the tablet protocol, X11 through its
tablets' XInput2 devices (mwin-0037).

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
`mwinSetGamepadTriggerRumble` drives the triggers' motors, on pads with
`mwin_padTriggerRumble`, apart from the others. On pads with
`mwin_padMotion`, `mwinSetGamepadMotion` turns the sensors on and
`mwinGetGamepadMotion` reads the latest acceleration and rotation rate
with the angle turned since the last read, so a program aiming with the
gyro reads once a frame and loses no turn.

On Linux gamepads are evdev devices. On Windows, Xbox pads come through
Windows.Gaming.Input, any number of them with their names and
batteries, or through XInput's four players where the runtime is
missing; the runtime may give a program in the background no input.
Other pads come through Raw Input and the HID parser. On macOS and iOS
they come through GameController, which maps every pad it knows (from
macOS 11.3), with the motors through CoreHaptics. On Android they are
the system's input devices, mapped by place where Android names a south
face button, with rumble and batteries from Android 12. On the web they
come through the Gamepad API.

## 7. Clipboard, drag and drop

The clipboard holds UTF-8 text. `mwinRequestClipboardWrite` and
`mwinRequestClipboardRead` are requests of a window. The browser may
ask the user first, and on Wayland only a focused window may use it. A
read that completes done leaves its text for `mwinGetClipboardText`,
which takes the read's request from the completion record and answers
`mwin_errorStale` once a later read found other text.
Text from other programs has ill-formed UTF-8 replaced with U+FFFD, and
text past `clipboardBytes` completes the read too large.

Beside text, `mwinRequestClipboardWriteData` offers data by MIME type,
up to four items, one of which may be the text; the bytes pass through
as they are, so an image goes as PNG the program encodes.
`mwinRequestClipboardReadData` asks for one type and
`mwinGetClipboardData` copies it out; a clipboard without it completes
the read failed. On X11 and Wayland, `mwinRequestPrimaryWrite` and
`mwinRequestPrimaryRead` keep the selected text, pasted with the middle
button, apart from the clipboard.

While something is dragged over a window, the program hears
`mwin_eventDragEntered`, `DragMoved` and `DragLeft`; a drop brings
`mwin_eventDropped` instead of the leaving. Its files, as UTF-8 paths
each ended by a NUL, and its text wait under the drop's number for
`mwinGetDroppedFiles` and `mwinGetDroppedText` until the next drop.
On Android, whose documents have no paths, a drop's documents are
copied into the application's cache first, and the drop comes once
every copy is whole.

## 8. Platform services

These are requests of a window, answered like any other:

- `mwinRequestFileDialog` opens the platform's file dialog: open, open
  several, save, or choose a folder, with filters by extension. After a
  done completion, `mwinGetDialogFiles` copies out the paths. It uses
  the common item dialog on Windows, a sheet on its window on macOS
  (which never blocks), the document picker on iOS (files opened as the
  application's copies; a save exports an empty file to the place
  chosen, which the program then writes), the document picker on
  Android (documents opened, copied into the application's cache
  between frames; no save or folder), and the desktop portal (else
  zenity) on Linux. The web has none.
- `mwinRequestOpenUrl` opens an http, https or mailto address in the
  user's program for it.
- `mwinRequestRevealFile` shows a file in the file manager (none on
  Android).
- `mwinRequestKeepAwake` keeps the display on while the window shows.

`mwinShowMessageBox` needs no context. It shows a platform message box
and waits, for errors a program must report before or after it has
windows. Android has none.

## 9. Accessibility hooks

The accessibility tree is Maul UI's; the window only hands it to the
platform:

- On Windows, `mwinRequestAccessibilityRoot` gives UI Automation the
  program's root provider.
- On macOS it makes the program's NSAccessibility root the window
  view's child; on iOS, its UIAccessibility root the view's element.
- On Android the root is the program's `AccessibilityNodeProvider`,
  which the activity's view gives; a root that also implements
  `maul.window.Explorer` is explored by touch, the library finding the
  node under the finger through it.
- On the web, each window has a host element over its canvas for the
  program's ARIA elements. Its selector is in the native handles. When
  a screen reader moves the focus to an element there, the window keeps
  the focus and the program still gets the keys (mwin-0035); a field
  there keeps its own text.
- `mwin_eventAccessibilityRequested` comes the first time a client asks
  a window for its tree. A program can build its tree only then.

## 10. Handing a window to a GPU layer

`mwinGetNativeHandles` gives the window system's own handles for a
window, as opaque pointers:

- Win32: the `HWND` and `HINSTANCE`;
- macOS and iOS: the `NSView` or `UIView` and its `CAMetalLayer`;
- Android: the `ANativeWindow`, its `ANativeActivity` and the
  activity's view;
- Wayland: the `wl_display` and `wl_surface`;
- X11: the XCB connection and window;
- the web: the canvas's selector.

A GPU layer makes its surface from them, and the library includes no
graphics API header. With Maul RHI the program copies the bundle into
the surface source of its platform, field for field:

<!-- guide: not run: Maul RHI's types; maul-rhi's seam check (test/seam) runs this copy -->
```c
mwinNativeHandles handles;
if (mwinGetNativeHandles(context, window, &handles) == mwin_success &&
    handles.platform == mwin_platformX11)
{
    mrhiSurfaceSourceXcb source = {
        .chain = {.type = mrhi_structSurfaceSourceXcb},
        .connection = handles.handles.x11.connection,
        .window = handles.handles.x11.window,
    };
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &source.chain;
    // mrhiCreateSurface(instance, &def, &surface), then configure it
    // at the window's pixel size (mwinWindowState.pixelSize).
}
```

The other platforms copy the same way: Win32's `hinstance` and `hwnd`,
Wayland's `display` and `surface`, Android's `window`, Apple's
`layer`, and the web's `selector` and `selectorLength`. Maul RHI's
guide (section 9) has the whole copy, and its `test/seam` check runs
it against this library. A lost surface (`mwin_eventSurfaceLost`) ends
a generation of handles, and the next arrives with
`mwin_eventSurfaceRestored`: when `surfaceGeneration` changes, the
program makes a new surface; when the pixel size changes, it
configures the surface again.

## 11. Backends and building

| Backend | Platform API | Built when |
| --- | --- | --- |
| Win32 | user32, Raw Input, IMM32, the touch keyboard's InputPane, OLE drag and drop, Windows.Gaming.Input, XInput | Windows |
| macOS | AppKit, Text Input Sources, GameController, CoreHaptics, IOKit | macOS (`MAUL_WINDOW_MACOS`) |
| iOS | UIKit with scenes, GameController, CoreHaptics | iOS (`MAUL_WINDOW_IOS`) |
| Android | NativeActivity with the library's Java, the input queue, the choreographer | Android (`MAUL_WINDOW_ANDROID`) |
| Wayland | xdg-shell and its extensions, the desktop portal, evdev | Linux (`MAUL_WINDOW_WAYLAND`) |
| X11 | XCB, XInput 2.1, XKB, XDND, IBus and Fcitx 5, the desktop portal, evdev | Linux (`MAUL_WINDOW_X11`) |
| Web | an HTML canvas and the browser's APIs | Emscripten, or wasm32-wasi |

On Linux one build carries both. At run time Wayland is chosen where
`WAYLAND_DISPLAY` names a compositor, else X11. The Linux system
libraries (libwayland, libxcb, libxkbcommon, libdbus) are loaded when a
context starts, so a program runs where they are missing, without that
backend. The build options are in the [README](../README.md).

On iOS the program's Info.plist carries a `UIApplicationSceneManifest`
(it may name no configuration: the library gives every scene its own
delegate), a `UILaunchScreen` so that the application fills the screen,
and `UIApplicationSupportsIndirectInputEvents` for a mouse's clicks to
come as the cursor's. `test/ios/Info.plist.in` is such a list.

On Android the application carries the library's Java
(`java/maul/window`, compiled with the program's own) and names the
library's activity in its manifest, with the program's native library
as its `android.app.lib_name`; the program defines `mwinAndroidMain`
and exports it from that library. No `configChanges` are needed: an
activity made anew joins the running program. An application that
shrinks its code with R8 or ProGuard, as a Gradle release build does,
adds `java/proguard-rules.pro` to its rules: the native library finds the
library's Java classes and members by name, and an accessibility root's
`virtualViewAt` by reflection. `test/android/` holds such a manifest,
and `tools/build_android_app.sh` builds an application without Gradle,
shrunk by R8 with those rules.

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
