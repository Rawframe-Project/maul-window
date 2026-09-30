# mwin-0021. Generic gamepads on Windows

Status: Accepted

## Context

The Win32 backend read gamepads through XInput alone (mwin-0009), so
DirectInput and generic HID pads were not seen at all. That covers
PlayStation and Switch pads over USB, arcade sticks and most third-party
pads. SDL_GameControllerDB has hundreds of Windows entries for them,
numbered as SDL numbers DirectInput's controls.

## Decision

- **Reading:**
  - The pads' code keeps a message-only window of its own. On it,
    `RegisterRawInputDevices` takes the Generic Desktop joystick,
    gamepad and multi-axis controller usages, with `RIDEV_INPUTSINK` and
    `RIDEV_DEVNOTIFY`.
  - `WM_INPUT_DEVICE_CHANGE` adds and removes devices, and
    `GetRawInputDeviceList` finds those present at the start; a device
    found twice is added once.
  - `WM_INPUT` reports are parsed with the HID parser (`hid.dll`, loaded
    at the start); without it there are no HID gamepads.
  - Devices with `IG_` in their path are XInput's, and left to it.
- **Numbering:** as SDL's DirectInput driver numbers them:
  - the axes X, Y, Z, Rx, Ry, Rz in usage order, whatever order the
    descriptor lists them in, then up to two of Slider, Dial and Wheel;
  - the hats;
  - the Button page's usages in order.
  Signed values are sign-extended from their bits. A range whose
  maximum reads as negative is the unsigned range of its bits. A hat's
  value outside its range is centred.
- **Mapping:**
  - `tools/gen_gamepad_db.py Windows` compiles the Windows entries into
    `src/generated/pad_windows.c` (867 devices, 398 mappings, about
    26 KB), and CI checks it as it checks the Linux tables.
  - A device is looked up as USB with its vendor, product and version,
    so another version's entry applies.
  - One the database lacks is raw.
- **Shared:** the Linux and Win32 backends post numbered controls
  through one module, `pad_map`, which holds the mapping arithmetic that
  was Linux's.
- **Facts:** the name is the product string, else "HID gamepad". There
  is no battery and no rumble, which generic HID does not define.
- **Tests:** the Raw Input and HID parser functions are a table, as
  XInput's are.

## Consequences

Programs see most gamepads on Windows, mapped where the database knows
them.

A process has one Raw Input target per usage. A program that registers
the joystick or gamepad usages itself takes them from the library.

- `win32_hid` runs against a stand-in on the test backend:
  - the devices that are gamepads and those that are not;
  - a PlayStation 4 pad mapped, with its axes numbered by usage;
  - an unknown stick raw, with its buttons in usage order and a signed
    axis;
  - hats in and out of range;
  - removal.
- `pad_map` checks the mapping arithmetic.
- The message window and the registration run in every Win32 test's
  context. No machine the tests run on has a HID pad.
