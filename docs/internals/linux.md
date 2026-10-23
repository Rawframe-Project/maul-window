# Linux

What the Wayland and X11 backends share. [Wayland](wayland.md) and [X11](x11.md) cover each window system.

## System libraries opened at run time

A Linux binary must run on a system with only X11, only Wayland, or neither library installed (a server, a container). So each context opens its system libraries with `dlopen` when it starts, keeps their functions in a table of its own, and closes them when it stops: `libwayland-client`, `libwayland-cursor`, `libxkbcommon`, `libxkbcommon-x11`, `libxcb` and its extensions ([wayland_api.c](../../src/wayland_api.c), [xkb_api.c](../../src/xkb_api.c), [x11_api.c](../../src/x11_api.c)). Nothing is process-wide, as the family allows no global mutable state. libdbus-1 is the exception that is opened once and never unloaded ([Services](services.md#the-session-bus)).

Building still needs the development files, for the headers and `wayland-scanner`. A shared build links with `--no-undefined`, so a call made by name instead of through the table fails the build.

## Choosing a backend

The native backend tries Wayland where `WAYLAND_DISPLAY` names a display, then X11 where `DISPLAY` names one. A backend that cannot reach its window system (its library missing, the connection refused) gives way to the next with a fresh context. A failed connection later stops the loop, and `mwinRun` returns `mwin_errorPlatform`.

## The keyboard

Both backends share one xkbcommon keyboard ([xkb_keyboard.c](../../src/xkb_keyboard.c)), fed by the compositor's keymap on Wayland and by XKB's state events on X11. A key's code comes from its evdev code ([evdev.c](../../src/evdev.c)), its meaning from the keymap's first level in the current layout group, and its text through the locale's compose table (`LC_ALL`, `LC_CTYPE`, `LANG`), so dead keys compose. Keys held when focus comes are not reported; focus that goes while keys are held posts a reset.

## Clicks

Neither Wayland nor X11 has a double-click setting, so a press of the same button within 500 ms and 4 units of the last counts as one more click ([clicks.h](../../src/clicks.h)).

## Gamepads, services and facts

Gamepads are evdev devices whatever the window system ([Gamepads](gamepads.md)). The services and system facts go through `xdg-open`, zenity or kdialog and the session bus, which inside a Flatpak or a Snap reach the desktop portal through the sandbox's own shims, so one path serves every desktop and sandbox ([Services](services.md)). External programs are started with `posix_spawnp`, never through a shell, and reaped at the pumps.
