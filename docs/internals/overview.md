# Overview

Maul Window is a core that keeps the contract (ids, requests, the event stream, limits) and one backend per window system that talks to the platform. The core never calls a platform API; a backend never touches the program's storage except through the core's functions. This page is the library's profile within the Maul family and the shape every other page assumes.

## Profile

- **Determinism.** What the platform delivers (the order and timing of input, the sizes and scales it chooses) is the platform's. Everything the library decides itself is deterministic: the same platform reports produce the same stream, which the test backend checks.
- **Threads.** The library starts no thread. A platform that calls back on a thread of its own (Windows.Gaming.Input's hotplug handler, CoreHaptics) only sets a flag the next pump reads. Functions the platform ties to one thread say `Main thread only.`.
- **Memory.** The context, created with the caller's allocator, owns every window, monitor, gamepad, cursor and queue in one block taken at creation, sized by `mwinLimits` ([context.c](../../src/context.c)). Nothing is allocated on the event path. Payloads whose size other programs or the user decide (clipboard text, a drop, a dialog's paths, a request's copied def) take a block of their own size when they arrive and give it back at the next one or at the context's end. Every block after the context's own goes through a wrapper of the caller's allocator that marks the context busy while it runs; each public function that takes a non-const context starts by refusing a busy context with `mwin_errorBusy`, so an allocator that calls back in changes nothing under the call it is serving.
- **Stale ids.** Ids outlive their objects by design: a destroyed window's records may still wait in the stream when the program reads them, and a slot is reused only once the record of its end has been drained. A stale id is not misuse: it counts nothing, and the call returns the stale status, `mwin_errorStale`. Only `mwin_errorInvalid`, `mwin_errorLimit` and `mwin_errorBusy` count one misuse (`mwinGetContextMisuse`).
- **Untrusted input.** Everything the platform hands over is untrusted: text is validated or repaired as UTF-8 through Maul Unicode, sizes are checked against a limit before anything is allocated, and the parsers of outside bytes (EDID, `text/uri-list`, zenity's output, pipes, compositions, X11 selection replies, Wayland data offers, the D-Bus replies and signals of the Linux services and input methods, the web page's records) are fuzzed.
- **Assertions.** `MWIN_ASSERT` ([core.h](../../src/core.h)) checks what only a bug in the library breaks, never untrusted input: in the core, request and window slots and their generations, the rings' counts and growing sequence, the text storage's bounds, and that a request slot is freed only by draining its one completion. It traps in debug, sanitizer and fuzz builds and compiles out of release builds.
- **Dependencies.** Per backend, the platform's own API and system libraries; on Linux they are opened at run time ([Linux](linux.md)). Maul Unicode is a private dependency: no public header includes it.

## The core and the backends

[backend.h](../../src/backend.h) is the whole interface. A backend:

- starts and stops its connection to the window system;
- runs the program's loop, or hands it to the platform;
- creates and destroys the platform window of a slot, and takes each new request of a window (`submit`);
- answers every request through `mwinComplete`, at once or at a later pump, and reports what the platform did through `mwinPost` and the core's reporting functions ([core.h](../../src/core.h)).

The core checks every argument before a backend sees it, answers what it can itself (a popup's mode request is unsupported everywhere, a superseded request completes at once), and turns the backend's reports into records: coalescing, merging, fitting compositions, validating text, counting resets. A backend therefore never has to defend the contract; it only translates.

Where a backend's logic needs no platform type (Android's pointer translator, the X11 touch and pen classification, the gamepad mapping, the EDID parser), it lives in a module of its own so that tests run it on every platform. [tools/modules.txt](../../tools/modules.txt) states which module may use which, and `tools/check_modules.py` enforces it.

## Who runs the loop

`mwinRun` checks the application def, creates the context and starts the backend. The core splits the program's life into `mwinStartProgram` (init), `mwinStepProgram` (one pump and one frame) and `mwinEndProgram` (quit and the context's end):

- **Win32, Wayland, X11 and the test backend** run the steps in a loop of their own. Their pumps never wait: they take what has arrived and return, and the program paces itself (usually by presenting with vsync).
- **macOS** runs AppKit's loop and steps from the display link or a timer; **iOS** runs `UIApplicationMain` and steps from a display link once a scene is in the foreground; **Android** steps from the choreographer, the platform having started the run; **the web** steps from `requestAnimationFrame`.

Some platforms wait for the program before they go on (Android before it takes a surface away, the web before a page is hidden, iOS before the application leaves the foreground). There the backend runs a frame at once from inside the platform's call (`mwinRunCriticalFrame`), so the program handles the critical record in time. A tick that comes while the program's own functions run is skipped, so frames never nest.

## How a request travels

1. The program calls `mwinRequest...`. The core validates it, takes a request slot of the window (`mwin_errorCapacity` when all are in flight), supersedes an older request of the same kind, copies what the platform may read later (a title, an icon, a dialog's def), and returns the id.
2. The backend's `submit` carries it out: at once where the platform answers synchronously (Win32's clipboard, most window changes), or at a later pump where it answers late (a portal's dialog, a compositor's configure, a browser's promise). [answer.h](../../src/answer.h) keeps a later answer tied to its request's slot and generation, so an answer never reaches a request that was superseded or whose window went.
3. The platform's notifications come first, then the completion, so the program sees the new state before it learns the request finished. A request slot is free again only once its completion is drained, so completions waiting never outnumber the slots.

## What lives where

- [The event stream](events.md): storage, coalescing, merging, resets and the limits that make "never dropped silently" hold.
- [Text input](text-input.md), [Gamepads](gamepads.md), [Clipboard and drops](clipboard-and-drops.md), [Services](services.md): the cross-platform parts and why each is shaped as it is.
- [Linux](linux.md) (shared by both Linux backends), [Wayland](wayland.md), [X11](x11.md), [Win32](win32.md), [Apple](apple.md), [Android](android.md), [Web](web.md): what each backend does that the code alone does not explain.
- [Maul conventions](maul-conventions.md): the family's rules on naming, errors, memory, threads and the ABI.
