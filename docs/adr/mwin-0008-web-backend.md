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
- **Keys** are known by `KeyboardEvent.code`, whose names are the
  key code enum's own, so a code names the same key on every layout.
  What a key means with no modifier comes from the Keyboard API's
  layout map where the browser has one (Chrome), and otherwise from
  the event's `key`, a letter in lower case, which is right for
  letters and not always for the rest under Shift. Text comes from
  keys that type one character, alone or with AltGr. Keys go to the
  program, the page's default actions stopped, except the browser's
  shortcuts: with Control or Meta, and F5, F11 and F12. A page is told
  no layout's name.
- **The pointer** comes as pointer events, so the mouse, touches and
  pens arrive apart and none is made of another. The mouse is
  captured to its canvas while a button is held; a button pressed or
  let go while another is held, which the DOM reports as a move, is a
  button record. Clicks are counted as on Wayland and X11. The wheel's
  pixels are detents of 100, its lines of 3. A pen's down and up are
  its tip's: a barrel button pressed while it hovers is a
  `pointerdown` in the DOM and a button record here.
- **Cursors:** shapes are CSS cursors and a hidden cursor is `none`. A
  captured cursor is a pointer lock, asked for without the system's
  acceleration where the browser can, which the browser grants only
  after a user's gesture; its motion comes as raw deltas and no cursor
  records. The user can end a lock with Escape; the window's mode is
  then what the program asked for until it asks again. A page cannot
  confine the pointer: confined modes are unsupported.
- **Facts:** the color scheme and reduced motion from media queries,
  the preferred languages from `navigator.languages`, each read again
  when the page says it changed. The accent, the text scale and power
  are not known to a page. A hidden page occludes its windows.

## Consequences

The web tests run in headless Chrome through puppeteer
(`test/web_runner.cjs`), which serves the build, gives the page a
canvas of its own, and carries out what the test asks: a new device
pixel ratio or color scheme, keys, the mouse and the wheel through
puppeteer, and touches and a pen through the DevTools protocol. Input
methods, the lifecycle, the clipboard and gamepads follow in their own
changes.
