# mwin-0036. A monitor's HDR facts and variable refresh

Status: Accepted

## Context

A renderer that shows SDR content on an HDR output scales it by the SDR
white level and tone maps to what the display can show. `mwinHdrFacts`
had whether HDR output is on and three luminances in nits, and no
backend filled it; `variableRefresh` was set on Apple only. The
platforms do not tell the same things. Win32 tells whether advanced
color is on and the SDR white level (DisplayConfig), Android and
Wayland's color manager tell luminances in nits; macOS and iOS tell
only the extended dynamic range headroom, the peak over SDR white,
never absolute nits. The luminances on Win32 sit in `DXGI_OUTPUT_DESC1`,
a graphics API header Maul Window does not include; the monitor's EDID,
which Windows keeps in the registry and X11 shows as RandR's `EDID`
output property, carries them in its CTA-861.3 HDR static metadata
block.

## Decision

- `mwinHdrFacts` gains `headroom`, the peak over SDR white now: 1 for
  SDR output, 0 where unknown. Where a platform gives nits and a white
  level, the backend derives it; where it gives headroom alone, the
  nits stay 0. No backend invents a value the platform does not give.
- One EDID parser (`edid.c`) reads the HDR static metadata block, for
  Win32 and X11 alike. It checks every block's checksum and every data
  block's length against its extension, answers nothing for an EDID
  without the block, and is fuzzed as every parser of outside bytes is.
- Win32: advanced color and the white level from DisplayConfig, the
  luminances from the EDID in the monitor's registry key.
- X11: the luminances from the RandR `EDID` property, HDR never on (X11
  has no HDR output); variable refresh from the `vrr_capable` property.
  A change of either property is a monitor change.
- Wayland: `wp_color_manager_v1` where the compositor offers it,
  nothing otherwise.
- macOS and iOS: whether EDR is on and the headroom from the screen's
  EDR values, the nits 0.
- Android: the luminances and whether HDR can show from
  `HdrCapabilities`, the headroom from the HDR/SDR ratio on API 34 and
  later.
- The web: `known` and `active` from `dynamic-range: high`, nothing
  else.
- Variable refresh stays false where nothing tells it (Win32, Wayland,
  the web): not known to be available.

## Consequences

A program learns the white level to scale SDR content by and the
headroom to tone map to wherever its platform tells them, in one shape.
The nits are absent on Apple, as Apple gives none. CI runners have no
HDR screen: the EDID parser is checked against hand-made blocks and by
fuzzing, X11 through properties a test sets on Xvfb's output, the other
platforms for the shape of what they answer.
