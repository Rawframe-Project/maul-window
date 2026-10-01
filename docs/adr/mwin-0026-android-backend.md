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
- **Input** comes from the activity's input queue on the main looper;
  each event goes to the input method first (`preDispatchEvent`), and
  what it leaves comes to the window. A motion event becomes samples,
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
  back, home, menu, volume, media and the like) and keys with no code go
  on to Android; the program takes every other key and every motion, so
  that Android acts on none of them (an Escape left to it would go
  back). The layout has no name.
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
  text, Escape and a volume key, and the wheel from Android 13.
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
