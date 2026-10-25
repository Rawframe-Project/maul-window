# Win32

Windows' windows belong to the thread that made them, its sizes are in pixels of monitors with their own DPI, and moving or sizing a window runs a loop of Windows' own inside the message dispatch. The backend builds with clang-cl and with clang for MinGW.

## Linking and loading

The backend links `user32`, `shcore`, `imm32`, `advapi32`, `dwmapi` and `wtsapi32`, which every supported Windows has. What only newer systems or optional components have is loaded at its first need: Windows.Gaming.Input through `combase.dll`, XInput's newest DLL, `hid.dll`, msctf and the input pane, `uiautomationcore.dll`. `uiautomationcore.dll` is never unloaded, since UI Automation's objects may outlive the context.

## DPI

A context makes the process per-monitor DPI aware (version 2) when it starts, unless the process chose an awareness itself; it never overrides one the process chose. Sizes and places are logical, the pixels over the DPI's scale. A window that moves to a monitor of another DPI takes the size Windows suggests, which keeps its logical size, and posts the scale change.

## The loops Windows runs

While the user moves or sizes a window, and while a common item dialog shows, Windows runs a modal loop inside the dispatch. A timer on the window runs the program's frames from inside it, so rendering goes on and the program never knows. The family allows a thread only where the platform leaves no other way; here the timer is that way. A dialog is never shown from inside a frame: its request waits for the next pump.

## Windows and modes

One window class, `MaulWindow`, registered at the context's start; it carries the program's own icon (its first icon group), so a window given none shows what Explorer shows for the program's file. Borderless full screen is a popup window covering its monitor; leaving it restores the style and placement. Mode requests are answered at once, their records following from the messages. A focus request is denied when Windows keeps the foreground from the process.

Custom chrome keeps `WS_CAPTION` and the frame styles, so Windows keeps its shadow, snapping and animations: `WM_NCCALCSIZE` gives the client area the whole window (leaving out, when maximized, the frame Windows puts past the monitor), one line of DWM frame keeps the shadow, and `WM_NCHITTEST` answers from the hit regions. A maximize region answers `HTMAXBUTTON`, which is what makes Windows 11 show its snap layouts. The frame's mouse messages over the client area are translated back into the program's own pointer records.

Occlusion ([win32_occlusion.c](../../src/win32_occlusion.c)) is what Windows tells of a window hidden wholly: minimized (`WM_SIZE`), cloaked by DWM, as on another virtual desktop (`EVENT_OBJECT_CLOAKED` and `EVENT_OBJECT_UNCLOAKED` from a WinEvent hook out of context, which the thread's own message loop delivers, with `DWMWA_CLOAKED` read at the window's creation), the session locked (`WTSRegisterSessionNotification`) and the display off (`RegisterPowerSettingNotification` for `GUID_SESSION_DISPLAY_STATUS`, which also tells the state at once; dimmed counts as on). A window is occluded while any holds. Windows tells no window that others cover it: Chromium computes that by walking every window's region on a thread of its own, at each change WinEvents tell, and the library starts no thread, so it is left out.

Owned windows have their owner as the Win32 owner and no `WS_EX_APPWINDOW`, so they stay off the taskbar. Popups are `WS_POPUP` tool windows placed in their owner's client coordinates; a tooltip is also `WS_EX_NOACTIVATE` and topmost.

## Input

- Keys are known by scan code, extended keys apart; a key sent without one gets it from its virtual key. Print Screen, which Windows sends only released, is pressed and released together. F10 and Alt alone would enter the menu mode of a window with no menu, and Alt with a key would beep, so all three are kept from Windows, while Alt+F4 and Alt+Space stay its own.
- The mouse keeps its own messages; touch and pen come from `WM_POINTER`, which never reaches `DefWindowProc`, so Windows makes no mouse input of them. Turning the mouse into pointer messages would change what every mouse program sees.
- An input record carries the time its message happened, not the time it was handled: `GetMessageTime`, whose milliseconds on `GetTickCount`'s clock are turned into the performance counter's by their age, or for touch and pen the performance counter's value `POINTER_INFO` gives. A message waiting behind a long frame or a modal loop keeps its time. Notifications, and messages Windows sends rather than posts, carry the time they are handled.
- Quick clicks follow Windows' double-click time and distance for every button, rather than the class's `CS_DBLCLKS`, which covers only some.
- A captured cursor is hidden and clipped to a point, with raw input (registered at the first capture) giving the unaccelerated motion. A confined cursor is clipped to the client area while the window has focus, again after it moves.
- Input methods: [Text input](text-input.md#win32-imm32-not-a-tsf-text-store).

## The rest

- **Clipboard:** used at once while the request is submitted. Another program may hold it open for a moment, so opening is tried five times a millisecond apart. Text read is bounded by its memory's size, since another program's text need not end with a terminator.
- **System facts** come from where Windows' settings keep them (the registry's apps theme, accent and text size, the client area animation setting, the power status, the preferred UI languages). Any setting message re-reads everything and the core posts only what changed. The title bar follows the apps theme (Windows 10 20H1 and later).
- **HDR:** advanced color and the SDR white level from DisplayConfig, the luminances from the monitor's EDID in the registry. `DXGI_OUTPUT_DESC1` has them too, but the library includes no graphics API header.
- **Accessibility:** `WM_GETOBJECT` with `UiaRootObjectId` posts the notice and, with a root, answers through `UiaReturnRawElementProvider`. A destroyed window calls it with no provider, as Microsoft asks, so UI Automation lets go.
- **Services:** `ShellExecuteExW` without the shell's own error box, `SHOpenFolderAndSelectItems` for revealing (with Windows' separators, which the shell's parsing needs), and the thread's execution state for keeping awake.
