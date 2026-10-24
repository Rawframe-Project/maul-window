# Services

File dialogs, opening addresses, revealing files, keeping the display awake, message boxes, and the system's facts and locales. They are thin over each platform, but their contract rules are deliberate.

## Requests of a window

Every service but the message box is a request of a window, because the platforms need one: the portal and the file manager place what they show against it, a wake lock is held for a visible page, and a window's end is a clear point to let go. What the platform reads later (an address, a path, a dialog's def) is copied at the call into one block the request holds and gives back when it is answered, or at the context's end.

## Addresses and paths

An address must be `http://`, `https://` or `mailto:`, with no space, control character or DEL. Every platform hands it to another program (`xdg-open`, the portal's handler, `ShellExecuteExW`), which could take another scheme as a command to run a file, or split the address at a space. Refusing these at the call makes the service mean the same everywhere. A path must be absolute: the file manager does not share the program's working directory.

## Keeping awake

Keeping awake is a window's state, but on every platform it is one wish of the whole program: the backend holds the display while some live window asks and shows (visible, not minimized), which the core works out from the windows' state at each pump, and lets go at the end. Win32 sets the thread's execution state only when the wish changes; macOS holds a power assertion; iOS turns the idle timer off; Android sets `FLAG_KEEP_SCREEN_ON`; the web asks for a wake lock again whenever the page shows, since the browser releases it when the page hides.

On Linux, Wayland's idle inhibitor applies to one surface and only while it shows, so the window's own wish is the whole rule there. Where the compositor has none, and on X11, the wish goes over the session bus: `org.freedesktop.ScreenSaver.Inhibit` (KDE, Xfce, Cinnamon, MATE, xscreensaver), else the portal's `Inhibit` (GNOME and sandboxes). At the end, an answer still on its way is waited for a quarter of a second so what it holds can be let go.

## File dialogs

Filters are extensions only, matched in any case: every platform takes them (glob patterns for the portal, written case-insensitively since GTK's globs heed case; `*.png;*.jpg` for Windows; content types on Apple's systems), while arbitrary globs or MIME types would not mean the same everywhere.

- **Linux** ([linux_dialog.c](../../src/linux_dialog.c)): the portal's FileChooser. The answer is the Response signal on a request object whose handle token is chosen before the call, so an answer that comes before the call's reply is not missed. The parent is `x11:` and the window's id on X11; Wayland would need an exported handle (xdg-foreign), which the backend does not make. A dialog whose request goes is closed (`Request.Close`). Without a portal, zenity, run without a shell, its output read without blocking at each pump.
- **Win32:** the common item dialog runs a modal loop of its own until the user is done. The family allows a thread only where a platform leaves no other way, and here there is one: a dialog is never shown inside the program's frame (a request waits for the next pump), and inside its loop a timer on the owner runs the frames, as while a window is moved. From that timer a dialog whose request went is closed through its own window.
- **macOS:** a sheet, never blocking; macOS panels have no filter menu, so the backend adds one under the panel.
- **iOS and Android:** the document picker. iOS has no save panel, so a save exports an empty file of the offered name and answers with the place chosen, its security scope kept open for the program to write. Android's documents are content addresses, copied into the cache as drops are; saving and folders have no path a program could write through and are unsupported.

## The session bus

The Linux services, the X11 input methods and the system facts reach the session bus (and the system bus for UPower) through libdbus-1, opened at run time the first time a service needs it and never unloaded, since it keeps process-wide caches other libraries may share ([dbus_api.c](../../src/dbus_api.c), [linux_bus.c](../../src/linux_bus.c)). Its headers are not needed to build. The connections are private to the context and never end the process when the bus goes. The library never lets libdbus start a bus: the address comes from `DBUS_SESSION_BUS_ADDRESS`, else the user's socket in `XDG_RUNTIME_DIR`, else there is none. Calls are read without blocking at each pump, each with its own ten-second deadline, since libdbus's timeouts need a main loop the program does not have.

A reply or a signal is whatever the owner of a bus name sent, and any program on the session bus may own a name nothing else holds. libdbus ends the process on a read of the wrong type, so every argument's type is checked before it is read or entered, and a dict entry only through `mwinBusEntry`, which takes its key only when it is a string. A value of an unexpected type is passed over; a text scale a `float` cannot hold, or one that rounds to zero, is no scale. `fuzz_linux_bus` hands the services and the input methods messages of the shapes they expect, with other types in their place, through libdbus itself.

## Message boxes

A message box needs no context, so a program can report the error that stops it from starting. It must depend on nothing the program had to build.

- **Linux** has no system message box. The backend runs zenity, else kdialog, through `posix_spawnp` with the text as arguments and no shell, and zenity is told to take no markup, so nothing in the text is ever read as a command. Where neither is there, it draws a box of its own on the X server ([x11_message_box.c](../../src/x11_message_box.c)) in the core "fixed" font, which every X server has, at the cost of Latin-1 only.
- **macOS:** an `NSAlert` run modally; **iOS:** a `UIAlertController`, waited for by running the main run loop, which needs a scene in the foreground. Their buttons are English, as AppKit names only its own OK.
- **Android** has none: Android forbids waiting on the main thread.

## System facts and locales

The backends report facts; the core keeps them and posts only what changed. A fact a platform does not tell stays unknown, never guessed.

On Linux there is no single source. Locales come from the environment as glibc reads it for messages (`LANGUAGE` in order, then the messages locale, nothing under the C locale), converted to BCP 47 ([linux_locale.c](../../src/linux_locale.c)). The program's own environment, normally what the desktop set at login, is read at the start, when the context's empty list takes it. The desktop's languages are then followed ([linux_languages.c](../../src/linux_languages.c)), from two sources:

- **GNOME:** AccountsService on the system bus, which the power module shares. `FindUserById` gives the user's path, then `GetAll` gives its `Languages` (else `Language`). After that, `PropertiesChanged` brings changes, and the older `Changed` signal triggers another `GetAll`, which replaces one still on its way.
- **KDE Plasma:** `[Translations] LANGUAGE` in `plasma-localerc`, in `$XDG_CONFIG_HOME` or else `~/.config`. The folder is watched through inotify (`IN_CLOSE_WRITE | IN_MOVED_TO`, so a rename into place counts too), filtered on the file's name. The watch is opened with `IN_NONBLOCK | IN_CLOEXEC` and drained at every pump without waiting, as the buses are read, since the library has no poll set and no thread. The file is read at most once a pump however many events came, and once more after the queue overflowed (`IN_Q_OVERFLOW`). A file that is not a regular file, is longer than 4096 bytes or holds a NUL is left alone; a missing file or key means no language.

Each source's first word is only its baseline, so the environment's list stays at the start. A later word that differs from the source's last one replaces the list. A word in which no name converts to BCP 47 brings back the starting list rather than an empty one. `mwinSetLocales` posts `mwin_eventLocaleChanged` only when the list really changes, so a repeated signal or a rewrite with the same `LANGUAGE` posts nothing. A missing system bus, AccountsService, user or configuration folder means no live updates and no error. A reply of another shape is left alone, as other input from outside is. A `PropertiesChanged` that arrives in the same pump as the `GetAll` answer before it is taken after that answer, keeping their order.

The look and motion come from the Settings portal (`ReadAll` for the freedesktop, GNOME and KDE namespaces, then `SettingChanged`), low power from the portal's power profile monitor, and on battery from UPower on the system bus. All are asked for at the start without waiting and taken at a later pump, so a program sees unknown facts for its first frames and then a change record. A value of another type than its key's is left alone.
