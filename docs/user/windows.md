# Windows and monitors

## Creating a window

`mwinCreateWindow` returns the window's id at once, with the id of its creation request. The platform makes the window later: `mwin_eventWindowCreated` arrives, then the notifications of its first size, scale and mode, then the request's completion. A window whose creation fails stays an id until the program destroys it.

The def (`mwinDefaultWindowDef`) gives the title, the logical size, the mode (windowed, borderless full screen, minimized, maximized), visibility and the style bits: resizable, decorated, always on top and custom chrome. On the web it may also name an existing canvas by a CSS selector (`canvas`, `canvasLength`); otherwise the library makes one.

`mwinDestroyWindow` is immediate: the id is stale at once, the window's requests in flight complete as cancelled, its waiting notifications are dropped, and `mwin_eventWindowDestroyed` follows. Destroying a window destroys the windows it owns first.

Nothing closes a window by itself. The close button sends `mwin_eventCloseRequested`, and the program destroys the window or does not. On Android the Back key and gesture send the same record, so a program goes back a screen or ends.

## Sizes and state

A window has three sizes, each with its own notification:

- the logical size the program lays out in (`mwin_eventResized`);
- the size in pixels it renders at (`mwin_eventPixelSizeChanged`);
- the scale between them (`mwin_eventScaleChanged`, with the size the platform suggests for the new scale).

Places and sizes are logical units, measured down and to the right from the top left. Notifications that report state coalesce: a newer size replaces one still waiting, so a program may skip intermediate values. `mwinGetWindowState` always gives the latest state the stream has delivered: sizes, place, mode, focus, occlusion, visibility, the safe area, the part the on-screen keyboard covers, the style, the opacity and the surface generation.

`mwin_eventOccluded` says that nothing of the window can be seen, and `mwin_eventRevealed` that some of it can again: a hint to draw less, which only some platforms give.

| Platform | Occluded when |
|---|---|
| macOS | the window's occlusion state says no part of it is visible |
| Wayland | the compositor marks the toplevel suspended (xdg-shell 6), as it chooses: for example, another window covers it or the screen is locked |
| X11 | the X server reports the window wholly covered by other windows; never under a compositing window manager, which draws windows off screen, so the X server reports none covered |
| Web | the page is hidden |
| Android | the activity is stopped (another activity in front of it, the home screen, the screen off); a pause alone, as under a translucent activity or in split screen, leaves it revealed |
| iOS | the window's scene is in the background; an inactive scene, under Control Center or the app switcher, stays revealed |
| Windows | the window is minimized, cloaked (on another virtual desktop), the session is locked or the display is off; a window covered by other windows is never reported, since Windows tells it nowhere |

## Requests

`mwinRequestTitle`, `Size`, `Position`, `Mode`, `Visible`, `Focus`, `SizeLimits`, `AspectRatio`, `Style`, `Opacity` and `Icon` change a window. A later request of the same kind on the same window supersedes one in flight. What a platform cannot do it answers `mwin_outcomeUnsupported`:

- Wayland places no window and lets the compositor decide modes; a mode request is answered by the compositor's next configure. A focus request is answered done once activation was asked for; the compositor may still refuse it.
- X11 leaves modes to the window manager; a mode request is denied when the window manager does not show the mode within half a second, and unsupported without one.
- The web has no place, maximizing, minimizing or size limits; full screen needs a user's gesture there.
- On iOS a window is a scene's, which the system sizes and places; its title is what the app switcher shows. A program on an iPhone has one window. On an iPad, with `UIApplicationSupportsMultipleScenes` in the Info.plist, each further window is a new scene, made once iPadOS connects it (or failed after ten seconds), and a scene the user closes asks its window to close. Android has one window, the activity's, drawn edge to edge behind the system bars; `safeArea` in the window state says what they cover.
- A macOS window has no icon of its own, so an icon request sets the application's. A Windows window given no icon shows the program's own.

An icon is up to four RGBA images (straight alpha) of up to 256 pixels a side; the platform takes the smallest at least as large as the size it shows, else the largest.

## Owned windows and popups

- **Owned windows:** a def's `owner` makes an owned window, a dialog, which stays above its owner, off the taskbar, and goes with it.
- **Popups:** `kind` makes a menu (`mwin_windowMenu`), which takes the keyboard, or a tooltip (`mwin_windowTooltip`), which never does. A popup needs an owner and is placed at `position`, in logical units from its owner's client area, and moves with it. On Wayland the compositor may slide or flip a popup onto the output; its reported place is where it went. Popups are always windowed and undecorated, and the web has none.
- A menu is asked to close (`mwin_eventCloseRequested`) when the keyboard goes elsewhere; the program decides whether to destroy it.

## Custom chrome

A window with `mwin_styleCustomChrome` draws its own title bar and borders. `mwinRequestHitRegions` tells the platform which rectangles of the client area are caption, edges, corners, minimize, maximize and close (up to 16; a later one lies over an earlier one, and the rest is client). Then:

- a press on a caption or an edge moves or resizes the window through the platform, and the program hears neither the press nor its release;
- a double click on a caption maximizes or restores the window;
- the button regions stay the program's to draw and act on, and on Windows 11 the maximize region shows the snap layouts (`snapLayouts` in the system facts).

The web has no frame to drag and answers unsupported.

## Monitors

`mwinGetMonitors` and `mwinGetMonitorInfo` describe the connected monitors: name, bounds and work area in pixels, physical size, scale, refresh rate, whether each is the primary one, whether it can vary its refresh rate, and HDR facts. Variable refresh is a tristate: known on X11 where the driver gives `vrr_capable`, on macOS 12 and later, on iOS and on Android 16 and later; `mwin_unknown` on Windows, Wayland and the web, which do not tell. Hotplug and changes arrive as `mwin_eventMonitorAdded`, `mwin_eventMonitorRemoved` and `mwin_eventMonitorChanged`; a monitor connected again gets a new id.

The HDR facts are what the platform tells, never invented: whether HDR output is on, the SDR white level and the display's luminances in nits (Windows, Android, Wayland with the color manager, X11 from the monitor's EDID), and `headroom`, the peak over SDR white, wherever it can be known (macOS and iOS give only this). Wayland names no primary monitor and reports no work area.

## Handing a window to a GPU layer

`mwinGetNativeHandles` gives the window system's own handles for a window, as opaque pointers, and the library includes no graphics API header:

- Win32: the `HWND` and `HINSTANCE`;
- macOS and iOS: the `NSView` or `UIView` and its `CAMetalLayer`;
- Android: the `ANativeWindow`, its `ANativeActivity` and the activity's view;
- Wayland: the `wl_display` and `wl_surface`; X11: the XCB connection and window;
- the web: the canvas's CSS selector.

With Maul RHI the program copies the bundle into the surface source of its platform, field for field:

<!-- guide: not run: Maul RHI's types; maul-rhi's seam check runs this copy -->
```c
mwinNativeHandles handles;
if (mwinGetNativeHandles(context, window, &handles) == mwin_success &&
    handles.platform == mwin_platformX11)
{
    mrhiSurfaceSourceXcb source = {
        .chain = {.type = mrhi_structSurfaceSourceXcb},
        .connection = handles.handles.x11.connection,
        .window = handles.handles.x11.window,
    };
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &source.chain;
    // mrhiCreateSurface(instance, &def, &surface), then configure it
    // at the window's pixel size (mwinWindowState.pixelSize).
}
```

A surface can be lost: Android takes a window's surface away while the activity is stopped, and the web while the canvas is out of the document. `mwin_eventSurfaceLost` ends a generation of handles and `mwin_eventSurfaceRestored` brings the next. When `surfaceGeneration` changes, the program makes a new surface; when the pixel size changes, it configures the surface again.
