# Platforms

What each platform asks of a program beyond linking the library, and what is not obvious about running there.

| Platform | Backend | Built when |
|---|---|---|
| Windows | user32, Raw Input, IMM32, OLE drag and drop, Windows.Gaming.Input, XInput | Windows |
| macOS | AppKit, GameController, CoreHaptics | macOS (`MAUL_WINDOW_MACOS`) |
| iOS | UIKit with scenes, GameController, CoreHaptics | iOS (`MAUL_WINDOW_IOS`) |
| Android | NativeActivity with the library's Java, the input queue, the choreographer | Android (`MAUL_WINDOW_ANDROID`) |
| Wayland | xdg-shell and its extensions, the desktop portal, evdev | Linux (`MAUL_WINDOW_WAYLAND`) |
| X11 | XCB, XInput 2, XKB, XDND, IBus and Fcitx 5, the desktop portal, evdev | Linux (`MAUL_WINDOW_X11`) |
| The web | an HTML canvas and the browser's APIs | Emscripten, or Clang's wasm32-wasi |

## Windows

A program is made per-monitor DPI aware (version 2) when the context starts, unless it chose an awareness itself in its manifest or by a call. While the user moves or sizes a window, or a file dialog shows, Windows runs a loop of its own; the library runs the program's frames from inside it, so rendering goes on.

## Linux

One build carries the Wayland and X11 backends. At run time Wayland is chosen where `WAYLAND_DISPLAY` names a compositor, else X11 where `DISPLAY` names a server; a backend that cannot reach its window system gives way to the next. `libwayland-client`, `libxkbcommon`, `libxcb` and its extensions, and `libdbus-1` are opened when a context starts, so the program runs where any of them is missing, with less.

The services (opening addresses, revealing files, keeping awake, file dialogs, the desktop's preferences) go through `xdg-open` and the session bus, which inside a Flatpak or a Snap reach the desktop portal. Inside a Flatpak, knowing whether the device runs on battery needs the program's permission to talk to UPower on the system bus.

## macOS

`mwinRun` runs AppKit's loop: frames come from the screen's display link on macOS 14 and later and from a 60 Hz timer before it, and keep coming during a live resize and while a menu is open. A frame that takes long also holds up AppKit's events, which run on the same thread.

## iOS

`mwinRun` calls `UIApplicationMain` and never returns. The program's `init` runs when the application's first scene connects; frames follow the display while a scene is in the foreground. A program that stops ends with `quit`, while the application runs on until the system ends it. Going to the background posts `mwin_eventSuspending`, runs one frame at once, then posts `mwin_eventSuspended` and pauses the frames.

The program's Info.plist carries:

- a `UIApplicationSceneManifest`, which may name no configuration: the library gives every scene its own delegate;
- a `UILaunchScreen`, so the application fills the screen;
- `UIApplicationSupportsIndirectInputEvents`, so a mouse's clicks come as the cursor's rather than as touches.

`test/ios/Info.plist.in` is such a list. A process with no bundle identifier cannot be an application; `mwinRun` refuses it with `mwin_errorPlatform`.

## Android

The platform begins the run, not the program. The program defines `mwinAndroidMain`, returning the application def it would hand to `mwinRun`, and exports it from its native library; the library's activity calls it and runs the program on the main thread, frames following the display. `mwinRun` itself answers `mwin_errorUnsupported` there. An activity the system makes anew (a rotation, the user coming back) joins the running program: the window keeps its id and gets a new surface. Since everything runs on the main thread, a frame longer than five seconds is an "application not responding".

The application:

- compiles the library's Java (`java/maul/window`) with its own; nothing else on the Java side is needed, no AndroidX and no Gradle;
- names the library's activity, or a subclass, in its manifest, with its native library as `android.app.lib_name`; no `configChanges` are needed;
- keeps NativeActivity's entry when it links statically. The CMake target does this; a build without CMake passes `-Wl,-u,ANativeActivity_onCreate`;
- adds `java/proguard-rules.pro` to its rules when it shrinks its code with R8 or ProGuard, as a release build does: the native library finds the Java classes and members by name.

```xml
<activity android:name="maul.window.Activity" android:exported="true">
    <meta-data android:name="android.app.lib_name" android:value="myprogram" />
    <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
    </intent-filter>
</activity>
```

`test/android/AndroidManifest.xml` is a whole manifest, and `tools/build_android_app.sh` builds an application without Gradle.

Android gives a window that never showed a frame no touches (Android 14 and later), so a program draws before it expects input. Back, the key or the gesture, is the window's close request.

## The web

On the web the browser owns the loop. With Emscripten, `mwinRun` hands the frames to `requestAnimationFrame` and never returns; the library's JavaScript is inside its C files, so a program links the static library and passes nothing else to the linker. A window is a canvas: the page's own when the def names one by a CSS selector, otherwise one the library appends to the body.

Without Emscripten (Clang's wasm32-wasi), the backend's JavaScript comes as imports. The build writes `maul-window.mjs` beside the library (the target property `MAUL_WINDOW_WEB_GLUE` names it, and it is installed under `share/maul-window`). The page puts what it returns among the instance's `env` imports, and the program links with `-Wl,--export-table`:

```js
import { maulWindowImports } from "./maul-window.mjs";

let instance;
const imports = { env: { ...maulWindowImports(() => instance.exports) }, wasi_snapshot_preview1: wasi };
instance = (await WebAssembly.instantiateStreaming(fetch("program.wasm"), imports)).instance;
```

There `mwinRun` returns `mwin_success` once `init` succeeded, and the page's animation frames run the program on; its last frame still ends it with `quit`. A hidden page is suspended and gets no frames. Fullscreen and pointer lock need a user's gesture, and the browser's own shortcuts (with Control or Meta, and F5, F11, F12) stay the browser's.
