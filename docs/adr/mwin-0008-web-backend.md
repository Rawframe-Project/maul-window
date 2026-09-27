# mwin-0008. The web backend

Status: Accepted

## Context

The requirements put the web second among the platforms and let it
shape the contract (section 10). The browser owns the loop and calls
the page once a frame; a window is a canvas in a page the program may
not own; sizes are in CSS pixels at a device pixel ratio that changes
with zoom and screens; fullscreen and pointer lock need a user's
gesture; and every browser API is JavaScript.

## Decision

- **Glue:** the page's side is JavaScript in the library's C files
  (`EM_JS`), so a program links the static library and nothing else;
  there is no `--js-library` or pre-js to pass. An `EM_JS` function
  defines no symbol a linker looks for, so each file of them has one C
  function the backend calls, which takes the file, and its
  JavaScript, into the program. The state of a context is an object in
  `Module.mwinWeb`, a Map keyed by the context's address.
- **Delivery:** the page's listeners queue records in that object and
  never call into the program; the backend takes them at the start of
  each frame, in order, stamped with `performance.now()`.
- **The loop:** `mwinRun` calls init, then gives the frame to
  Emscripten's main loop on `requestAnimationFrame` and does not
  return, as W6 allows. The frame that stops the program ends the loop,
  calls quit and frees the context. The core's loop is split for this
  into start, step and end, which the pumping backends run in a loop
  of their own.
- **Windows:** a window is a canvas. A window def may name one of the
  page's by a CSS selector; otherwise the backend makes one of the
  def's CSS size and appends it to the body. A canvas gets an id if it
  has none, so its native handle is always `#` and the id. The page's
  canvas keeps its size, which the page's layout gives it, and gets
  its style back when the window goes; a made canvas is removed. A
  canvas takes focus (a `tabindex`) and keeps touches from scrolling
  the page (`touch-action: none`).
- **Size and scale:** the logical size is the canvas's CSS content box
  and the scale `devicePixelRatio`. The drawing buffer follows the box
  in device pixels: the browser's count of them
  (`devicePixelContentBoxSize`) where it agrees with the ratio to a
  pixel, the box times the ratio otherwise. Headless Chrome, emulating
  a ratio, reports the CSS size there, and a new box can come before
  the new ratio; so the ratio is looked at on a resolution media query,
  on a page resize and on every new box, again a frame later, and a
  new ratio sizes every drawing buffer again.
- **Requests:** a title sets the page's title and the canvas's
  `aria-label`; a size sets the canvas's CSS size; visibility, focus
  and opacity are the canvas's. Fullscreen is the Fullscreen API's:
  its answer comes from the page's event, and a request the browser
  refuses, as it does without a user's gesture, is denied. A page has
  no place for a window, no maximizing or minimizing and no size
  limits: those requests are unsupported.
- **The monitor:** the screen, one monitor, in device pixels, at the
  page's ratio; the browser tells no refresh rate.
- **Facts:** the color scheme and reduced motion from media queries,
  the preferred languages from `navigator.languages`, each read again
  when the page says it changed. The accent, the text scale and power
  are not known to a page. A hidden page occludes its windows.

## Consequences

The web tests run in headless Chrome through puppeteer
(`test/web_runner.cjs`), which serves the build, gives the page a
canvas of its own, and carries out what the test asks, such as a new
device pixel ratio or color scheme. The keyboard, the pointer, pointer
lock, touch and pen, input methods, the lifecycle, the clipboard and
gamepads follow in their own changes.
