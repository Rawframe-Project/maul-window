# Android

An Android program is an activity the Java VM starts. Native code runs when the activity calls into it, on the main thread, and the process outlives its activities: a configuration change or the user coming back destroys an activity and creates a new one in the same process. NativeActivity hands native code the window's surface, its input queue and the activity's callbacks, but its view has no input connection, and anything that subclasses or implements a Java type needs Java.

## The library's Java

The library ships one Java package, `maul.window` ([java/maul/window](../../java/maul/window)): `Activity` extends the platform's `NativeActivity`, and helpers reach what is Java only (input devices, the clipboard, the document picker, drags, facts, accessibility). No AndroidX and no Gradle requirement: the application compiles the package with its own code. The native library finds the classes and members by name through JNI and registers its native methods on the class it loads through the application's class loader. R8 and ProGuard see neither JNI nor reflection, so the library ships [java/proguard-rules.pro](../../java/proguard-rules.pro), which keeps the package whole and every `virtualViewAt(float, float)`, and the test applications are built by R8 with those rules, so a member reached by name without a rule fails the tests.

## The entry and the loop

The program defines `mwinAndroidMain` in place of a `main`; the library's `ANativeActivity_onCreate` calls it and runs the program. The library keeps no global: the activity reaches the backend through the call, and later activities find the running program through a static field of the Java activity.

Everything runs on the main thread: `init` inside the first activity's `onCreate`, the activity's callbacks, the input queue attached to the main looper, and the frames from the choreographer while the activity is started. The library starts no thread. An activity made anew joins the running program: the window keeps its id, its surface goes with the old activity and comes with the new, and `mwinAndroidMain` is not called again. A program that stops finishes its activity; a frame callback still waiting then frees the context, since the choreographer cannot take one back.

Android waits on some callbacks (a surface going) before it goes on, so the backend posts the lost surface and runs a frame at once ([Overview](overview.md#who-runs-the-loop)).

## Input

A motion event becomes samples, its history first, which [android_motion.c](../../src/android_motion.c) turns into records with no platform type, so it is tested on every platform: fingers as touches with ids that count up and are never reused, a stylus as the pen with tilt computed from Android's tilt and orientation as Chromium computes them, a mouse as the cursor. The NDK names the changed mouse button only from API 33, so buttons are those whose state changed. Android ends hovering before every press and starts it after, so the cursor enters at its first motion and is never said to leave.

Keys Android calls system keys (home, volume, media) go on to Android; Back and every other key, and every motion, are taken, so Android acts on none of them: an Escape left to Android would go back. Back itself is the window's close request when released (not when Android cancels it). The library does not opt into Android 13's back callback, so Back stays a key on every version.

While a window accepts text each event goes to the input method first; otherwise the method never sees the keys, as it would take some ([Text input](text-input.md)).

## Window and insets

The window has no title bar and is drawn behind the system bars, which are transparent; the safe area is the bars' and cutouts' insets, and the keyboard's part the input method's inset at the bottom. From Android 14 the input dispatcher gives a window an empty frame until its surface has a buffer, so a window that never showed a frame gets no touches.

## Accessibility

The root is the program's `AccessibilityNodeProvider`, which the library's view returns. Touch exploration's hovers reach the window's native input queue rather than its views (measured under TalkBack), so the work of AndroidX's `ExploreByTouchHelper` falls to the library: while there is a root, those hovers belong to the tree, the virtual view under the finger comes from the root's `maul.window.Explorer` or a public `int virtualViewAt(float, float)` (a provider of another library cannot implement the interface), and the views entered and left are announced.

## Facts

The theme comes from the configuration's night mode; the text scale, reduced motion (animator scale 0), the accent (Android 12's system palette), battery saver, the battery and the locales through the Java helper. They are read when an activity starts or changes its configuration, and the power every two seconds, since Android announces it only to broadcast receivers.
