# Documentation

## Using the library

- [Getting started](user/getting-started.md): requirements, building and installing, the build options, the model the API follows, results, ids, defs and limits.
- [Windows and monitors](user/windows.md): creating and changing windows, owned windows and popups, custom chrome, monitors, handing a window to a GPU layer.
- [Input](user/input.md): keys, text and input methods, the on-screen keyboard, the pointer, cursors, touch and pens.
- [Gamepads](user/gamepads.md): mapped and raw pads, rumble and motion.
- [Clipboard and drag and drop](user/clipboard-and-drops.md).
- [Services and system facts](user/services.md): file dialogs, addresses, files, keeping the display awake, message boxes, the user's preferences, accessibility hooks.
- [Platforms](user/platforms.md): what each platform asks of a program.
- [Testing a program](user/testing.md) on the headless test backend.
- [API reference](https://rawframe-project.github.io/maul-window/api.html), generated from the headers.

## How it works

- [Overview](internals/overview.md): the library's profile, the core and the backends, and how a request travels.
- [Glossary](internals/glossary.md).
- [The event stream](internals/events.md): storage, merging, critical records and the limits' arithmetic.
- [Text input](internals/text-input.md), [Gamepads](internals/gamepads.md), [Clipboard and drops](internals/clipboard-and-drops.md), [Services](internals/services.md).
- Backends: [Linux](internals/linux.md), [Wayland](internals/wayland.md), [X11](internals/x11.md), [Win32](internals/win32.md), [Apple](internals/apple.md), [Android](internals/android.md), [Web](internals/web.md).
- [Maul conventions](internals/maul-conventions.md): the rules every Maul library follows.

## Maintaining it

- [Development](operations/development.md): toolchain, building each platform, generated files, vendored data, fuzz findings.
- [Checks](operations/checks.md): the local tiers, the test labels, the CI jobs and their budgets.
- [Test environments](operations/test-environments.md): the display fixture, stand-ins, simulators, emulators and browsers.
- [Release](operations/release.md).
