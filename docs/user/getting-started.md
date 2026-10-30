# Getting started

Maul Window gives a program its windows, input, gamepads and the platform services around them, on Windows, macOS, iOS, Android, Linux (Wayland and X11) and the web. This page covers building the library, the model every part of the API follows, and the rules for results, ids and memory.

## Contents

- [Requirements](#requirements)
- [Building](#building)
- [The model](#the-model)
- [Results, ids and defs](#results-ids-and-defs)
- [Memory and limits](#memory-and-limits)
- [Where to go next](#where-to-go-next)

## Requirements

- CMake 3.25 and GCC 14 or Clang 19 or newer. On Windows, `clang-cl` (the Visual Studio component "C++ Clang tools for Windows"). On macOS and iOS, Xcode 16.
- Linux, for the Wayland backend: `wayland-scanner` and the development files of `wayland-client` 1.22, `wayland-protocols` 1.32 and `xkbcommon` 1.0 (`libwayland-dev`, `wayland-protocols`, `libxkbcommon-dev` on Debian and Ubuntu).
- Linux, for the X11 backend: the development files of `xcb`, `xcb-randr`, `xcb-xkb`, `xcb-cursor`, `xcb-render`, `xcb-xinput` and `xkbcommon-x11`; its tests also need `xcb-xtest` and `xcb-xfixes`.
- Android: the NDK r28 or newer. The web: Emscripten, or Clang's wasm32-wasi with wasi-libc.

The library depends on [Maul Unicode](https://github.com/Rawframe-Project/maul-unicode), which validates every string that crosses it. An installed `maul-unicode` package of the pinned version is used when CMake finds one; otherwise the build fetches that release and installs it alongside.

The oldest systems the backends run on: Windows 10 version 1703, macOS 11, iOS 15, Android 11 (API 30). On Linux the system libraries are opened when a program starts, so a program runs where one is missing, without that backend. [Platforms](platforms.md) has what each platform asks of a program.

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix <prefix>
```

A program then finds it with `find_package(maul-window REQUIRED CONFIG)` and links `maul-window::maul-window`, or uses the `maul-window` pkg-config module.

A program that builds Maul Unicode itself can check that its version is the one Maul Window is built and tested with, before anything else, since Maul Unicode's package takes only its exact version: an installed Maul Window names it in `maul-window_UNICODE_VERSION`, set by `find_package(maul-window)` even when the Maul Unicode it then looks for is another version, and a Maul Window built from source with `add_subdirectory` or FetchContent names it in the cache variable `MAUL_WINDOW_UNICODE_VERSION`.

| Option | Default | What it does |
|---|---|---|
| `MAUL_WINDOW_WAYLAND`, `MAUL_WINDOW_X11` | on for Linux | The Linux backends; one build carries both. |
| `MAUL_WINDOW_GAMEPAD` | on | The gamepad component. Without it a call to a gamepad function fails to link. |
| `MAUL_WINDOW_TEST_BACKEND` | with the tests | The headless test backend ([Testing a program](testing.md)). |
| `MAUL_WINDOW_BUILD_SHARED` | off | A shared library instead of a static one. |
| `MAUL_WINDOW_BUILD_TESTS`, `_SAMPLES`, `_BENCH` | when top level | The tests, `samples/window.c` and the benchmarks. |

`samples/window.c` opens a window and prints what the platform says about it; F11 switches to full screen, T accepts text, Escape ends it.

## The model

A program describes itself in an application def and hands it to `mwinRun`:

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
    while (mwinContext_NextEvent(context, &event) == mwin_success)
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

`mwinRun` creates the context, calls `init` once, calls `frame` once per frame until it returns `mwin_frameStop`, calls `quit` if the def has one, and destroys the context. Who runs the loop depends on the platform: the library pumps Win32, Wayland and X11 itself, while AppKit, UIKit, Android and the browser call the frames. A program cannot tell the difference, with one exception: on iOS and the web `mwinRun` may never return, so cleanup belongs in `quit`. [Platforms](platforms.md) has the details.

Three rules hold everywhere:

- **One stream.** Everything the platform says arrives in one ordered stream of records, across windows, which `frame` drains with `mwinContext_NextEvent`.
- **Requests and completions.** Every change to a window is a request. The call returns an `mwinRequestId` at once, and exactly one `mwin_eventRequestCompleted` answers it later, after the notifications the change caused. The outcome is `mwin_outcomeDone` or one reason from a closed list: unsupported, denied, superseded (a later request of the same kind replaced it), cancelled (its window went first), failed, too large.
- **No threads, no callbacks.** The library starts no thread and calls the program only through `init`, `frame` and `quit`.

## Results, ids and defs

Every fallible function returns an `mwinResult`: `mwin_success`, or `mwin_errorInvalid` (a bad argument), `mwin_errorStale` (an id whose object is gone), `mwin_errorCapacity` (a limit of `mwinLimits` is full, or memory), `mwin_errorUnsupported`, `mwin_errorPlatform`, `mwin_errorState`, `mwin_errorVersion`, `mwin_errorLimit` (an argument past a fixed bound with a named constant, such as `MWIN_MAX_ADDRESS_BYTES`, which more room would not lift) or `mwin_errorBusy` (a call made from inside the context's allocator). The values are the Maul family's: a status means the same in every Maul library. `mwinGetResultName` names each one for diagnostics.

A call on the context that fails leaves nothing from before in its outputs: a value is zero, an id the null id and a request the null request. The exception is a call refused with `mwin_errorBusy`, made from inside the context's allocator while another call uses it: `mwinContext_NextEvent` and `mwinGamepad_GetMotion` leave their record as it was, since it may be that other call's. The id of what a call makes, a window, a cursor, a monitor, a gamepad or a request, is the null id on every failure, busy included. A buffer too short for text or bytes gets none of them, so it never holds text cut inside a character; the call answers `mwin_errorCapacity` with the length it needs. A list of ids that does not all fit, from `mwinContext_GetGamepads` or `mwinContext_GetMonitors`, is a success: the first ones are written and the count is the total.

Windows, monitors, gamepads, cursors and requests are ids with a generation. An id outlives its object harmlessly: using it afterwards answers `mwin_errorStale`, and a slot is reused only once the record of its end has been drained.

Each `mwin_errorInvalid`, `mwin_errorLimit` or `mwin_errorBusy` a live context returns also counts as one misuse, which `mwinContext_GetMisuse` reads. A release build can watch the count to catch a program's bugs; stale ids count nothing.

Defs carry a cookie. Build each with its `mwinDefault...` function and change the fields; a def made any other way is refused. The application def and the message box def, which need no context, also carry the version of the headers the program was built with, and `mwinRun` and `mwinShowMessageBox` refuse a def built against another release with `mwin_errorVersion` before reading anything else of it.

## Memory and limits

Every limit on memory and work is a field of `mwinLimits` in the context def, with a default: 8 windows; per window 32 requests in flight, 256 notifications, 256 input records per class and 4,096 bytes of text; 16 monitors; 8 gamepads; 1 MiB of clipboard text; 256 files and 1 MiB per drop and per dialog; 16 cursors. A request past a limit is refused with `mwin_errorCapacity`.

The context takes its memory when it is created, through the def's allocator; nothing is allocated on the event path. Payloads whose size the user decides (clipboard text, a drop, a dialog's paths) take a block of their own size when they arrive and give it back at the next one. The allocator calls nothing that changes the context: while it runs, every function that takes a non-const context refuses with `mwin_errorBusy`.

## Where to go next

- [Windows and monitors](windows.md): creating windows, their requests and sizes, owned windows and popups, custom chrome, monitors, and handing a window to a GPU layer.
- [Input](input.md): keys, text and input methods, the pointer, cursors, touch and pens.
- [Gamepads](gamepads.md).
- [Clipboard and drag and drop](clipboard-and-drops.md).
- [Services and system facts](services.md): file dialogs, addresses, files, keeping the display awake, message boxes, the user's preferences and accessibility hooks.
- [Platforms](platforms.md): what each platform asks of a program.
- [Testing a program](testing.md) on the headless test backend.
