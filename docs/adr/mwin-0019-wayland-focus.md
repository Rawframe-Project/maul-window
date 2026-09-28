# mwin-0019. Focus requests on Wayland

Status: Accepted

## Context

The Wayland backend answered focus requests with unsupported. Core
Wayland lets no client move focus, which prevents focus stealing. The
xdg-activation protocol lets a client pass the compositor proof of the
user's action: a token made from an input event's serial. A launcher
hands its token on through `XDG_ACTIVATION_TOKEN`. Window decision W10
and its research note record the protocol.

## Decision

- **A focus request:**
  - The backend asks `xdg_activation_v1` for a token. It gives the
    latest input serial with the seat, and the surface that has the
    keyboard.
  - When the token comes, it activates the window's surface with it
    and answers the request done. The compositor says nothing back, so
    there is nothing more to wait for, as on X11.
  - A newer request drops a token still coming. The core has already
    answered the older request superseded.
  - A window gone lets its token go.
- **A tooltip** is denied focus, as on X11.
- **Without the global,** focus requests are unsupported.
- **The launcher's token:** the first window shown activates itself
  with `XDG_ACTIVATION_TOKEN`, and the variable is removed so that the
  programs it starts do not take it.

## Consequences

Programs can bring a window forward in answer to the user's input, and
windows started from a launcher come up focused where the compositor
agrees. The compositor still decides, so a done answer means the
activation was asked for, not granted.

The `wayland_focus` test runs against a compositor with an activation
global of its own:

- the launcher's token, and the variable gone;
- a token's serial, seat and surface, and the activation with it;
- two requests in a row, the first superseded and only the second
  activating.

The `wayland_owned` test checks a tooltip denied and the request
unsupported without the global.
