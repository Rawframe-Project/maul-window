# Web

The browser owns the loop and calls the page once a frame; a window is a canvas in a page the program may not own; sizes are CSS pixels at a device pixel ratio that changes with zoom and screens; fullscreen and pointer lock need a user's gesture; and every browser API is JavaScript.

## One copy of the JavaScript

The page's side is JavaScript inside the library's C files as `EM_JS` functions, so an Emscripten program links the static library and passes nothing else to the linker. An `EM_JS` function defines no symbol a linker looks for, so each file of them has one C function the backend calls, which pulls the file and its JavaScript into the program.

A plain WebAssembly program (Clang's wasm32-wasi, as Rawframe's engine is) has nothing that compiles JavaScript into it and nothing that unwinds its stack. For it, [web_js.h](../../src/web_js.h), the one file that includes Emscripten's headers, defines `EM_JS` as the declaration of a function imported from the module `env` under its own name, and [tools/gen_web_glue.py](../../tools/gen_web_glue.py) reads the same C files and writes `maul-window.mjs`, whose `maulWindowImports(exports)` returns those functions. A small runtime at its top gives them what Emscripten's would (views of memory, made again when memory grows, string conversion, the function table). Every function's name starts with `mwin`, so it meets no other library's among the imports. The JavaScript therefore cannot drift between the two builds; a construct the generator does not understand fails the build, not the page.

## The loop

With Emscripten, `mwinRun` hands the frame to Emscripten's main loop on `requestAnimationFrame` and never returns. Without it, `mwinRun` hands its frames to the page's `requestAnimationFrame` and returns while the program runs on, since it cannot keep the stack; the last frame ends the program as Emscripten's does. Time comes from `performance.now()` in both.

Records are queued by the page's listeners in an object of the context's own (`Module.mwinWeb`, keyed by the context's address) and taken at the start of each frame. Only the life cycle calls the backend from inside the browser's event: a hidden page gets no animation frames and may never come back from `pagehide`, so the listener posts suspending and runs the program's frame at once.

## Canvas, size and scale

The logical size is the canvas's CSS content box and the scale `devicePixelRatio`. The drawing buffer follows the box in device pixels: `devicePixelContentBoxSize` where it agrees with the ratio, the box times the ratio otherwise. Headless Chrome emulating a ratio reports CSS pixels there, and a new box can arrive before the new ratio, so the ratio is looked at on a resolution media query, a page resize and every new box, again a frame later, and at each pump besides. A canvas taken out of the document loses its surface until it is back.

## Input and focus

- Keys are known by `KeyboardEvent.code`, whose names are the key codes'. A key's meaning comes from the Keyboard API's layout map in Chrome, else the event's `key` in lower case, which is right for letters and not always for the rest under Shift. Keys reach the program with the page's default actions stopped, except the browser's own shortcuts.
- Pointer events keep the mouse, touches and pens apart. A button pressed while another is held comes as a move in the DOM and is turned back into a button record. The wheel's pixels are detents of 100, its lines of 3.
- A window accepting text moves the focus into an invisible text field at the caret ([Text input](text-input.md)). The canvas, its field and the elements of its accessibility host hold the window's focus as one, so a screen reader moving among the program's ARIA elements is no focus change, and keys bubbling to the host still reach the program. Keys on the rest of the page stay the page's: a canvas shares its page with content that is not the program's.

## Services

Addresses open with `window.open`, the new tab's `opener` cut before it loads; `noopener` would do the same but hide whether a popup blocker stopped the tab. The clipboard is the async Clipboard API, `NotAllowedError` being denied. A page never names files, so revealing and file dialogs are unsupported, and drops give file names only. The screen is held by a wake lock, asked for again whenever the page shows.
