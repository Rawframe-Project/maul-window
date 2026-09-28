# mwin-0018. System facts and locales on Linux

Status: Accepted

## Context

The X11 and Wayland backends reported no system facts and no preferred
locales. Linux has no one system call for either. Programs take their
languages from the environment, and desktops serve their look and
motion through the Settings portal. Window decision W9 and its
research note record the sources.

## Decision

- **Locales** are read once, at the start, from the environment, as
  glibc reads it for messages:
  - `LANGUAGE` in order, then the messages locale (the first set of
    `LC_ALL`, `LC_MESSAGES` and `LANG`);
  - nothing under the C locale or none, as glibc then ignores
    `LANGUAGE`;
  - each name as a BCP 47 tag, the codeset dropped, `@latin`,
    `@cyrillic` and `@valencia` kept as script and variant, other
    modifiers dropped, UN M.49 territories kept;
  - names of no language and repeats left out, and tags that do not fit
    left out whole.
- **Look and motion** come from `org.freedesktop.portal.Settings` on the
  session bus:
  - `ReadAll` is sent at the start for `org.freedesktop.appearance`,
    `org.gnome.desktop.interface` and `org.kde.kdeglobals.KDE`, and taken
    at a later pump. `SettingChanged` is followed after that.
  - The theme is `color-scheme`, and no preference is unknown.
  - The accent is `accent-color`, and a value out of range is none.
  - Reduced motion is `reduced-motion` where the portal has it, else
    GNOME's `enable-animations` off or KDE's `AnimationDurationFactor`
    of 0 (as a number or as text).
  - The text scale is GNOME's `text-scaling-factor`, else 1.
  - A value of another type than the key's is left alone.
  - Changes post `mwin_eventThemeChanged` through the core.
- **Power:**
  - Low power is the portal's `power-saver-enabled`
    (`org.freedesktop.portal.PowerProfileMonitor`).
  - On battery is UPower's `OnBattery`, on the system bus, which the
    backend opens by its address (`DBUS_SYSTEM_BUS_ADDRESS`, else
    `/run/dbus/system_bus_socket`).
  - Each is read with `Properties.Get` at the start, taken at a later
    pump, and followed through `PropertiesChanged` of its own interface.
    Other interfaces, keys and types are left alone.
  - Changes post `mwin_eventPowerChanged`.
- **No bus or no portal:** the facts stay as they were, and nothing is
  started to answer.

## Consequences

Every Linux program now connects to the session bus and the system
bus at the start, where they are, as GTK and Qt programs do. A program
sees unknown facts for its first frames, then a change event as each
answer comes. Inside a Flatpak sandbox, the system bus needs the
program's permission to talk to UPower; without it, on battery stays
unknown.

The tests:

- `linux_locale` checks the conversion.
- `linux_services` checks the locales a backend reports.
- `linux_settings` runs the X11 backend against a portal of its own:
  - GNOME's and KDE's answers;
  - each kind of change;
  - a value of the wrong type;
  - no answer.
- `linux_power` runs it against a portal and UPower of its own, on a
  session and a system bus:
  - both facts at the start and as they change;
  - changes of other interfaces, keys and types;
  - a refusal and no system bus.
