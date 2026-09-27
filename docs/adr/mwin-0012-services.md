# mwin-0012. Platform services

Status: Accepted

## Context

The requirements (section 9) ask for what a game or an editor needs
from the system around its windows: opening a URL in the default
browser, revealing a file in the file manager, inhibiting the
screensaver while a game runs, and a native modal message for fatal
errors before any UI exists. File dialogs are the same kind of service
but large enough for a record of their own (mwin-0013). Every platform
may ask the user before opening an address, or answer late (the
desktop portal, the browser's wake lock), and some cannot do a service
at all (a page reveals no files).

## Decision

- **Requests of a window:** `mwinRequestOpenUrl`,
  `mwinRequestRevealFile` and `mwinRequestKeepAwake` are requests like
  the others (mwin-0002): each is answered by exactly one completion, a
  later one of the same kind supersedes one in flight, and a platform
  without the service answers `mwin_outcomeUnsupported`. A window
  carries them because the platforms do: the portal and the file
  manager take the window to place what they show, a wake lock is held
  for a visible page, and a window's end is a clear point to let go of
  the display.
- **Addresses** are `http://`, `https://` or `mailto:` in any case,
  with something after the scheme, and no space, control character or
  DEL. A platform may hand the address to another program (the
  portal's handler, `xdg-open`, `ShellExecuteW`), which could take any
  other scheme as a command to run a local file or an application, and
  split an address at a space. Refusing these at the call makes the
  service mean the same on every platform: a program that wants to
  open a local file reveals it instead.
- **Paths** are absolute: a leading `/`, or on Windows a drive or a
  share. A relative path would depend on the program's working
  directory, which the file manager does not share.
- **Text** of both is UTF-8 without NULs, at most `MWIN_ADDRESS_BYTES`
  (4096), copied at the call into a block from the context's
  allocator and released when the request completes, or at the
  context's end for one still held.
- **Keeping awake** is the window's state (`awake` in
  mwinWindowState), set when the request completes with success, as a
  mode or a visibility is. On every platform it makes one wish of the
  whole program: a backend holds the display while some live window
  asks for it and shows (visible, not minimized), which the core
  answers from the windows' state at each pump, and lets it go at its
  end.
- **Win32:** an address goes to `ShellExecuteExW` with the window as
  the owner and without the shell's own error box, so a scheme with no
  handler fails through the completion alone. A path takes Windows'
  separators, since the shell's parsing refuses `/`, and Explorer opens
  its folder with it selected (`SHParseDisplayName`,
  `SHOpenFolderAndSelectItems`); a file that does not exist fails. The
  display is held by the thread's execution state
  (`ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED`), set
  only when the wish changes.
- **Web:** an address opens in a new tab through `window.open`, whose
  `opener` is cut before the tab loads, so the page it opens cannot
  reach this one; `noopener` would do the same but hide whether a
  popup blocker stopped the tab, which is answered as denied. A page
  never names files, so revealing one is unsupported. The screen is
  held by a wake lock (`navigator.wakeLock`), and keeping awake is
  unsupported without one. The browser releases a lock when the page
  is hidden, so the backend asks for one again when the page shows,
  and again at once for a lock that came already released.
- **Message boxes** need no context: `mwinShowMessageBox` shows a
  modal box with a title, a message, a kind (information, warning,
  error) and buttons (OK, OK and Cancel, Yes and No), waits for the
  user and says whether the box was accepted. A program reports with
  it the error that stops it from starting, so it depends on nothing
  the program had to build. The def has a cookie like the others; the
  title is at most 512 bytes and the message 8192, UTF-8 without NULs.
  - **Win32:** `MessageBoxW`, task-modal and brought forward.
  - **Web:** `alert`, or `confirm` for two buttons, with the title as
    the first line, since a page's boxes have none.
  - **Linux** has no system message box, so the backend runs `zenity`,
    or `kdialog` where zenity is missing, through `posix_spawnp` with
    the text as arguments and no shell, so nothing in the text is ever
    read as a command; zenity is told to take no markup. Neither
    present answers `mwin_errorUnsupported`, and the program falls
    back to its standard error.

## Consequences

The test backend records the addresses and paths it was asked to open
(`mwinTestGetOpened`) and answers keeping awake with success; the
contract tests cover the refusals, superseding, the awake state, a
refusal leaving it, and text released with a window or at the context's
end under the sanitizers. The Linux message box is tested with stand-in
zenity and kdialog scripts on the path. Each backend's services follow
in their own change; until then the backends answer
`mwin_outcomeUnsupported`.
