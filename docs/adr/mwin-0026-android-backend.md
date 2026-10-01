# mwin-0026. The Android backend

Status: Accepted

## Context

The requirements name Android among the platforms, with the
`ANativeWindow` that Maul RHI's Vulkan driver presents to. An Android
program is an activity the Java VM starts; native code runs when the
activity calls into it, on the main thread, and the process outlives
its activities: a configuration change or the user coming back
destroys an activity and creates a new one in the same process. The
platform's `NativeActivity` hands native code the window's surface, its
input queue and the activity's callbacks, but its view has no input
connection, so input methods cannot reach it; anything that subclasses
or implements a Java type needs Java. Android waits for some callbacks
(a surface going) before it goes on, which the contract's critical
records answer (mwin-0002). The family's libraries start no threads and
keep no global state.

## Decision

- **Oldest Android:** 11 (API 30), for `WindowInsets.Type`; NDK r28 or
  newer, whose clang 19 has the family's C23. arm64-v8a and x86_64 are
  tested.
- **The activity:** the library ships one Java class,
  `maul.window.Activity` (`java/maul/window`), which extends the
  platform's `android.app.NativeActivity`: no AndroidX, no Gradle
  requirement. The program's manifest names it, or a subclass of it,
  with the program's native library as `android.app.lib_name`. The
  class gains what C cannot do as the backend needs it; it holds the
  running program in a static field, so that a new activity finds it.
- **The entry:** the program defines `mwinAppDef mwinAndroidMain(void)`
  in place of a `main` that calls `mwinRun`; the library's
  `ANativeActivity_onCreate` calls it and runs the program as `mwinRun`
  would, handing the backend the activity (the context's launch, which
  `mwinRun` leaves null: there it returns `mwin_errorUnsupported`). The
  library keeps no global: the activity reaches the backend through the
  call, and later activities find the program through the Java field.
  A shared build of the library finds the entry in the program's
  library, which exports it.
- **The loop:** everything runs on the main thread: init inside the
  first activity's `onCreate`, which then returns; the activity's
  callbacks; its input queue, attached to the main looper; and the
  frames, from `AChoreographer_postFrameCallback64` at each vsync while
  the activity is started. The library starts no thread.
- **The life cycle:** the activity stopping is the application
  suspending, told in a frame of its own, then suspended, and frames
  pause; its start resumes them the same way. A suspended record still
  waiting when the application resumes gives way to the resuming one.
- **Activities made anew** join the running program: the window keeps
  its id, its surface going with the old activity and coming with the
  new, and `mwinAndroidMain` is not called again. An activity the user
  or the program finishes tells the window it was asked to close. A
  program that stops, here or in a critical frame, ends with quit and
  finishes its activity; a frame callback still waiting then frees the
  context, as the choreographer cannot take one back. A later activity
  calls `mwinAndroidMain` anew, in the same process.
- **The window:** one, the activity's, which the system sizes and shows
  with the activity. It is created at the activity's first native
  window, which its create request waits for; each later native window
  is a new surface generation (`mwin_eventSurfaceLost` when one goes,
  in a frame of its own while Android waits, and
  `mwin_eventSurfaceRestored` when the next comes). Its size in pixels
  is the native window's, its scale the configuration's density over
  160; its focus the activity window's. A second window, sizing,
  placing, restyling, hiding it or setting its mode are unsupported.
- **Native handles** give the `ANativeWindow` and the `ANativeActivity`,
  through which the program reaches Java and its assets.
- **Input** comes from the activity's input queue on the main looper.
  While the window accepts text each event goes to the input method
  first (`preDispatchEvent`; it composes from a keyboard's keys too), and
  what it leaves comes to the window; otherwise the input method never
  sees the keys, as it would take some of them (Escape, seen with
  Gboard). A motion event becomes samples,
  its history first, which a translator with no platform type
  (`android_motion.c`) turns into records:
  - a finger (or a tool Android does not know) is a touch, its id
    counting up from the first and never reused, as Android's pointer
    ids are; pressure is kept to 0..1;
  - a stylus or its eraser is the pen, hovering and touching, its barrel
    button Android's primary stylus button, its tilts toward x and y
    from Android's tilt and orientation as Chromium computes them;
  - a mouse is the cursor. Its buttons are those whose state changed at
    Android's button press and release (the NDK names the changed button
    only from API 33); a press with no button state (an injected one) is
    the left button, and the pointer's going up releases what is held.
    Quick clicks count within Android's double tap timeout. Android ends
    hovering before every press and begins it again after
    (crbug.com/715114), so the cursor enters at its first motion over
    the window and is never said to leave. Its scrolling is the wheel,
    in Android's detents.
- **Keys:** a key's code comes from its scan code where a keyboard gives
  one (the Linux input code, through the table the Linux backends use),
  else from its Android key code through Android's generic key layout,
  compiled by `tools/gen_android_keys.py` from `tools/androidkeys/`
  (Generic.kl and the NDK's keycodes.h, Apache 2.0). Its meaning is what
  the device's key character map types for it without modifiers, known
  once pressed under the current layout, as on iOS; a key that types
  nothing is named. A press types its text through the key character map
  unless Control or Meta is held, a dead key's accent joining the next
  character. The keys Android calls system keys (`KeyEvent.isSystemKey`:
  home, menu, volume, media and the like) but Back, and keys with no
  code, go on to Android; the program takes every other key and every
  motion, so that Android acts on none of them (an Escape left to it
  would go back). The layout has no name.
- **Back,** the key or the gesture Android turns into one, is the
  window's close request when it is let go (not when Android cancels
  it): the program decides, ending, which is what Back at the root
  means, or going back within itself, as a desktop program may ask
  first. The contract has no Back of its own, and an Escape key a
  program could ignore would leave the user no way out. The library
  does not opt into Android 13's back callback, so Back stays a key on
  every version.
- **Input methods:** the library's activity replaces NativeActivity's
  content view with a focusable view that is a text editor, whose input
  connection keeps only the composition (the program keeps its own
  text), as SDL's does: committed text comes as text, a newline and a tab
  as the Enter and Tab keys' press and release, as on iOS; a composition
  (one underlined segment, its selection in bytes) is told while the
  window accepts text, and dropped, the input method started again, when
  it stops; a deletion outside a composition is the Backspace and Delete
  keys, and the keys an input method sends and its editor action come as
  keys. The Java activity calls the library's native methods, which the
  backend registers at its start on the class it loads through the
  application's class loader. The caret's place is not told to the input
  method.
- **The on-screen keyboard** shows while the program asks for it
  (`InputMethodManager.showSoftInput`, again when the window gets the
  focus), its purpose setting the input type: text, an address or a URL
  with nothing suggested, corrected or capitalized, a signed decimal
  number, a password. A keyboard asked for is not shown again by an
  activity made anew.
- **Insets:** the window has no title bar, whatever the theme, and is
  drawn behind the system's bars (`setDecorFitsSystemWindows(false)`,
  transparent bars). The safe area is the bars' and cutouts' insets, and
  the part the keyboard covers the input method's inset at the window's
  bottom, both in logical units, told as they change and when the window
  is made.
- **Gamepads** are the devices, not virtual, whose sources include the
  gamepad or the joystick, listed through the library's Java helper
  (`maul.window.Gamepads`: `InputDevice` is Java only) every half second
  and at once when an event comes from one not followed; their keys and
  joystick motions come through the input queue and are taken, so
  Android's fallbacks (B as Back) do not follow. One with the south face
  button (`BUTTON_A`) is mapped as SDL maps Android's gamepads: A south,
  B east, X west, Y north, Back as select, Menu as start, the hat as the
  dpad; the left stick on X and Y, the right on RX and RY where both are
  there, else on Z and RZ (Android's layouts for the gamepads it knows);
  the triggers on LTRIGGER and RTRIGGER, else BRAKE and GAS, else Z and
  RZ beside a right stick on RX and RY, else the L2 and R2 keys. Any
  other device is raw: the gamepad keys it has, numbered in the
  library's order, and its joystick axes. A key is a gamepad's by its
  device and code, since Android counts a joystick's buttons
  (`BTN_TRIGGER` and up) as keyboard keys. Android gives an axis 0 until
  its device reports it, which a trigger at rest on a centered axis
  never does, so such a trigger stays at rest until it moves. Rumble
  runs the device's motors (two where Android 12 lists them, else one
  at the stronger), timed by Android; batteries are read from Android
  12 on, with the look.
- **Services:** the clipboard holds text, written as plain text and
  read as the first item's text at once, through the library's Java
  helper (`maul.window.Services`); from Android 10 only the focused
  application reads it, and a refused read, which Android tells apart
  only in its log, reads as empty, as on iOS. An address opens in the
  application the user chose (an `ACTION_VIEW` intent), answered at
  once, failed when there is none. Keeping awake is the activity
  window's `FLAG_KEEP_SCREEN_ON` (`ANativeActivity_setWindowFlags`),
  which holds while the window shows and is set again on an activity
  that joins. There is no file manager to show a file in, and no message
  box: Android forbids waiting on its main thread, where a program
  waiting on one would stop its own input (Android calls an application
  not responding after five seconds).
- **File dialogs** show the system's document picker
  (`ACTION_OPEN_DOCUMENT`, through `maul.window.Documents`). Android's
  documents are content addresses, and a document's descriptor cannot
  be opened again through its `/proc/self/fd` path (refused on Android
  11 and 15: it points into shared storage), so each chosen document is
  copied into the application's cache, a folder per dialog and per
  document under its provider's name, between frames (at most 8 MiB a
  frame, from a descriptor that never blocks), and the dialog answers
  with the copies' paths once they are whole, as iOS's picker copies its
  documents in. Saving and choosing a folder are unsupported: neither
  has a path a program could write through. A dialog asked for while
  another waits supersedes it: the old picker is finished by its request
  code, its copy stops, and an answer under its number is dropped.
  Filters offer the types Android knows of their extensions. The copies
  of earlier runs are removed at the start.
- **Drops** come through a drag listener (`maul.window.Drops`) on the
  activity's view, which covers the window: a drag with a text type
  carries text, one with any other type files (a document is an address
  of any type); entering, moving and leaving are told as they come, in
  logical units. A drop's documents are opened under the drag's
  permissions (`requestDragAndDropPermissions`, which another
  application's documents need) and copied into the cache as a dialog's
  are, a folder per drop, and its first text item is its text; the drop
  is told once every copy is whole, at its place. The window takes no
  drag while the last drop's documents are still copied, rather than
  queue drops.
- **Accessibility:** the root is the program's
  `AccessibilityNodeProvider`, which the library's view returns as its
  provider (held by a global reference, given by each activity's view in
  turn; the first asking is the program's
  `mwin_eventAccessibilityRequested`), and the view joins the native
  handles for the nodes to name. Touch exploration's hovers, whose tool
  is a finger, reach the window's native input queue rather than its
  views (measured under TalkBack), so ExploreByTouchHelper's work falls
  to the library: while there is a root those hovers are the tree's, the
  virtual view under the finger comes from the root's
  `maul.window.Explorer` (a provider's own nodes do not expose their
  children, so the library cannot find it alone), and the views entered
  and left are announced.
- **System facts:** the theme from the configuration's night mode;
  through the library's Java helper (`maul.window.Facts`) the text scale
  (the font scale), reduced motion (Android's "remove animations" makes
  the animator scale 0), the accent (Android 12's system palette, none
  before), battery saver and whether the battery provides the power
  (the battery's sticky broadcast), and the preferred locales
  (`LocaleList`). They are read when an activity starts (a joining
  activity starts too) or changes its configuration, and the power every
  two seconds, Android announcing it only to receivers.
- **A window that never showed a frame gets no touches** from Android 14
  on: the input dispatcher gives the window an empty frame until its
  surface has a buffer (seen on Android 15; Android 11 used the window's
  frame). A program that draws is not affected; the input test shows a
  frame drawn on the CPU first.
- **Tests:** `android_motion` drives the translator on every platform
  with samples as the backend reads them. In the emulator, run on
  Android 11 and 15 here and on 11 in CI, `android_input` takes what the
  system's input command injects: a tap, a swipe, a mouse's tap, a
  stylus's tap (a pen where the command marks it a stylus), keys, typed
  text, Escape and Menu, and the wheel from Android 13; `android_ime`
  calls the activity's input connection as an input method would and
  shows and hides the keyboard, with the safe area and the keyboard's
  part, and each purpose's input type; `android_pad` makes three USB
  HID devices through Android's `hid` tool and the kernel's uhid, as a
  real gamepad comes: an Xbox 360 pad's ids (Android's own layout), a
  gamepad with none (the generic layout, a hat) and a joystick (raw),
  and checks their facts, records, state and removal; `android_services`
  writes and reads the clipboard, keeps awake and stops, opens an
  address in the browser and comes back, and checks that an activity
  made anew keeps the display awake; `android_dialog` shows the picker,
  supersedes it and cancels with Back, then hands the activity the
  picker's result for two documents it made in Downloads and reads their
  copies back, a large one over several frames; `android_drop` starts a
  drag from its own window under a finger the runner puts down, moves
  and lifts, with a MediaStore document and text, and checks the drag's
  records, the drop's place, its copy and its text; `android_system`
  checks the facts at the start, unplugs the battery and turns battery
  saver on, changes the night mode and the font scale (told through the
  activity made anew), and presses Back; `android_access` sets a test
  provider as the root, enables an accessibility client of the test's
  own that asks for touch exploration, has it read the tree through the
  system, puts a finger down through the emulator's console (whose
  touches pass the system's touch explorer, as `input` does not), and
  checks what the client heard and that the program saw no touch. The
  runner turns the system's
  animations off while a test runs, so that an opening transition does
  not move the window under the test's input, and the night mode, the
  font scale, battery saver, the battery and the enabled accessibility
  services are put back after a test.
- **The emulator runners:** `cmake/android-emulator.cmake` wraps
  the NDK's toolchain file and runs executables through
  `tools/run_android.sh` (`adb push` and `adb shell`); window tests are
  applications, built without Gradle by `tools/build_android_app.sh`
  (javac and d8 for the Java, aapt2, zipalign, apksigner) and run by
  `tools/run_android_app.sh`, which installs and starts one, carries out
  the system actions the test asks for (the home key, a rotation) and
  waits for its closing result line. A CI job boots the emulator on the
  Linux runner and runs every test there.
- **Slices:** the first the build, the runners, the activity, the
  entry, the loop, the life cycle, the window and its surface; then
  touch, mouse, pen and keys; input methods, the keyboard's area and
  safe areas; gamepads; the clipboard, addresses, keeping awake, alerts,
  document pickers and drops; facts, locales, accessibility and back.

## Consequences

A program's Android build adds one function and one manifest entry,
and keeps its state in statics or on the heap, as on the web without
Emscripten. The library's Java is one source directory the program's
build compiles with its own; nothing else on the Java side is required.
Because everything runs on the main thread, a frame longer than
Android's input timeout (5 seconds) is an "application not responding",
as it would be for any Android program.
