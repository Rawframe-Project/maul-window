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
- **Input** the backend does not handle yet goes on to the input method
  and the activity unhandled, so that none waits.
- **Tests** run in the emulator: `cmake/android-emulator.cmake` wraps
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
