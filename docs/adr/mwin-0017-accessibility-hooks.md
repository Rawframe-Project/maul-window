# mwin-0017. Accessibility hooks

Status: Accepted

## Context

The requirements (section 9) leave the accessibility tree and its
platform adapters to Maul UI. The window only cooperates with them,
through typed hooks that know nothing of the tree:

- answering `WM_GETOBJECT` on Win32;
- exposing the view's accessibility children on macOS;
- hosting a DOM subtree next to the canvas on the web.

Window decision W8 and its research note record what each platform
asks of a window.

## Decision

- **The root:**
  - `mwinRequestAccessibilityRoot` hands the platform the root of the
    window's tree, which the program implements, or no root.
  - Once the platform took it (a completion of done), the core keeps it
    in the window.
  - A refused root leaves the one before.
  - A new window, even in a slot that had one, starts with none.
- **The notice:** `mwin_eventAccessibilityRequested` comes the first
  time an accessibility client asks a window for its tree, once a
  window. A program can build its tree and send updates only once there
  is a client, as AccessKit's lazy activation does.
- **Win32:**
  - The root is an `IRawElementProviderSimple*`.
  - `WM_GETOBJECT` with `UiaRootObjectId` posts the notice. With a root,
    it answers through `UiaReturnRawElementProvider`, from
    `uiautomationcore.dll`, loaded at the first need and kept loaded,
    since UI Automation's objects may outlive the context.
  - Other object ids are left to Windows.
  - A window that answered calls `UiaReturnRawElementProvider(hwnd, 0,
    0, NULL)` as it is destroyed, as Microsoft asks, so UI Automation
    lets go of what it kept.
- **X11 and Wayland** answer unsupported. AT-SPI is a service of the
  application on the accessibility bus. Its adapter takes the window's
  place, size and focus from the window's state and events.
- **The web:**
  - The root request is unsupported. Instead, each window has a host
    element for the program's ARIA elements:
    - made right after the canvas;
    - absolutely placed over it and kept there as the canvas or the
      page resizes;
    - letting the pointer through;
    - hidden with the canvas and removed with the window.
  - `mwinNativeHandles` gives its selector: the canvas's, with
    `-accessibility` after it.
- **The test backend:**
  - It takes roots.
  - `mwinTestAskAccessibility` plays a client asking, so a program can
    test its adapter's lazy start without a platform.

## Consequences

The contract test covers the root the platform takes or refuses, the
single notice, a reused slot, and the refusals.

On Win32, the test gives a provider of its own:

- A client takes it from the answer with `ObjectFromLresult`, and UI
  Automation holds it until the client lets go.
- Other object ids tell nothing.

UI Automation keeps no provider that raised no event, so the release
at destruction cannot be seen from a test. It follows Microsoft's
documented contract.

The web test checks the host in headless Chrome:

- its place over the canvas, after a resize too;
- that it lets the pointer through;
- that it hides with the canvas;
- that it goes with the window.
