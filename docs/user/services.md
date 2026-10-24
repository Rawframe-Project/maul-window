# Services and system facts

The services around a program's windows are requests of a window, answered by a completion like every other: the platform may ask the user first or answer late, and a platform without the service answers `mwin_outcomeUnsupported`. The window is the one the platform places what it shows against.

## File dialogs

`mwinRequestFileDialog` opens the platform's dialog for one file, several files, a file to save, or a folder (`mwinDefaultFileDialogDef`), with a title, a starting folder, the name a save dialog offers, and filters. Frames go on while it shows.

- Filters are extensions without dots, separated by `;` (`"png;jpg"`), matched in any case, so they mean the same everywhere. Folder dialogs ignore them; no filters offers every file.
- After a done completion, `mwinGetDialogFiles` copies out the chosen paths under the request's id, absolute, UTF-8, each ended by a NUL. Closing the dialog completes the request cancelled; more than `dialogFiles` paths or `dialogBytes` bytes completes it too large.

| Platform | The dialog |
|---|---|
| Windows | The common item dialog, owned by the window. |
| macOS | An open or save panel as a sheet on its window, with the filters as a menu under it. |
| Linux | The desktop portal's file chooser over the session bus, else zenity. |
| iOS | The document picker. Opened files are the application's copies; a save exports an empty file to the place chosen, which the program then writes. |
| Android | The document picker; documents are copied into the application's cache between frames. No save and no folder. |
| The web | None: a page's file input gives contents, never paths. |

## Addresses, files and keeping awake

- `mwinRequestOpenUrl` opens an `http://`, `https://` or `mailto:` address in the user's program for it. Other schemes, spaces and control characters are refused at the call: the platform hands the address to another program, which could take another scheme as a command. A program that wants to open a local file reveals it instead.
- `mwinRequestRevealFile` shows a file, by its absolute path, in the file manager with the file selected. iOS, Android and the web have no file manager to show it in.
- `mwinRequestKeepAwake` keeps the display on while the window shows and is not minimized; `awake` in the window state says it holds. Over all windows it is one wish of the program, let go when no live, shown window asks.

## Message boxes

`mwinShowMessageBox` shows a modal box with a title, a message, a kind (information, warning, error) and buttons (OK, OK and Cancel, Yes and No), waits for the user, and says whether the box was accepted. It needs no context: a program reports with it the error that stops it from starting. So its def, like the application def, carries the version of the headers the program was built with, and a def built against another release is refused with `mwin_errorVersion`.

On Linux it runs zenity or kdialog; where neither is there it draws a plain box of its own on the X server `DISPLAY` names, in the core "fixed" font, so text beyond Latin-1 shows as "?". Without any of them it answers `mwin_errorUnsupported`, and the program falls back to its standard error. Android has no message box: it forbids waiting on its main thread.

## System facts and locales

`mwinGetSystemFacts` gives the user's preferences and the power state:

- the theme (light, dark or unknown), the accent color, reduced motion and the text scale;
- whether the device runs on battery and whether it saves power, each true, false or unknown;
- whether Windows shows snap layouts over a maximize region.

Changes arrive as `mwin_eventThemeChanged` and `mwin_eventPowerChanged`. `mwinGetPreferredLocales` gives the user's languages in order as BCP 47 tags separated by commas (`de-DE,en-GB`), and `mwin_eventLocaleChanged` says they changed.

Not every platform knows every fact. On Linux they come from the desktop portal, UPower and the environment, so they are unknown for the first frames and then arrive as change records. The locales on Linux come from the program's own environment, which is normally what the desktop set at login (`LANGUAGE=fr ./game` gives `fr`). They are read once at the start: a language changed in the desktop's settings while the program runs is not seen, and `mwin_eventLocaleChanged` comes at most once, at the start, never after. A page knows no accent, text scale or power saving. A fact a platform does not tell stays unknown, never guessed.

## Accessibility hooks

The accessibility tree belongs to the program (Maul UI builds one); the window only hands its root to the platform:

- `mwinRequestAccessibilityRoot` gives the platform the program's root: UI Automation's provider on Windows, an NSAccessibility object on macOS, a UIAccessibility object on iOS, an `AccessibilityNodeProvider` on Android. A root that also implements `maul.window.Explorer`, or has a public `int virtualViewAt(float x, float y)`, is explored by touch on Android.
- `mwin_eventAccessibilityRequested` comes the first time a client asks a window for its tree, so a program can build its tree only once someone needs it.
- On the web each window has a host element over its canvas for the program's ARIA elements; its selector is in the native handles. While a screen reader moves the focus among the elements there, the window keeps its focus and its keys, and a field there keeps its own typed text.
- On Linux an AT-SPI adapter is a service of the application on the accessibility bus; it needs nothing from the window but its state and events, so the root request is unsupported there.
