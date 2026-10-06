# mwin-0033. The touch keyboard and input purpose on Win32

Status: Accepted

## Context

`mwinRequestVirtualKeyboard` shows or hides the on-screen keyboard and
tells the platform what the field takes. Win32 answered it
unsupported, though Windows tablets and convertibles have a touch
keyboard. Desktop windows reach it through `IInputPaneInterop`
(Windows 10 1607 and later), which gives a window's `InputPane`, whose
`TryShow` and `TryHide` are best effort: Windows shows the keyboard
only where no hardware keyboard is attached and the window is in the
foreground. What a field takes is a window's input scope, which msctf's
`SetInputScope` sets without a TSF text store (mwin-0032); the touch
keyboard and the input method read it.

## Decision

- The Win32 backend sets the window's input scope from the purpose
  (number, e-mail address, password, URL, or the default), then asks
  the window's `InputPane` to show or hide.
- The request completes `mwin_outcomeDone` when Windows took it,
  `mwin_outcomeDenied` when it declined (a hardware keyboard, a window
  in the background), `mwin_outcomeUnsupported` where Windows has no
  `InputPane` for desktop windows.
- msctf and the runtime's activation are loaded at the first request,
  so nothing is linked and older Windows run as before.
- The part of the window the keyboard covers comes from
  `IFrameworkInputPane` (Windows 8 and later), which a window advises
  at its first request: its handler gets the keyboard's place on the
  screen in pixels as it shows and nothing as it hides, and the
  backend reports the part of the client area it covers, in logical
  units, as `mwin_eventVirtualKeyboardChanged`. This classic COM
  interface gives pixels on the screen; the InputPane's own
  `OccludedRect` would need a runtime delegate and leaves its units
  for desktop windows undocumented.

## Consequences

A program on a Windows tablet shows the touch keyboard for its own text
fields, with the layout the purpose asks for. On a desktop with a
keyboard the request is denied, which a program may ignore.
