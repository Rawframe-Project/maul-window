# mwin-0035. Keys and focus on the web's accessibility host

Status: Accepted

## Context

On the web each window keeps a host element over its canvas for the
program's ARIA elements (`accessibility.h`). A screen reader in focus
mode moves the DOM focus to the element its user is at, and keys go to
that element. The backend listened for keys and focus on the canvas and
its hidden text field only, so the focus moving to the host counted as
the window's focus lost, and keys there reached no listener: the
program lost its focus and its keys exactly while a screen reader user
worked its interface. Flutter's web engine listens for keys on the
whole page; a Maul Window canvas shares its page with the page's own
content, so keys on that content must not reach the program.

## Decision

- The canvas, its text field and every element in its accessibility
  host have the window's focus as one: the focus moving among them is
  no change, leaving for the page is a focus lost, coming back a focus
  gained.
- Keys bubbling to the host reach the program as key events, as keys on
  the canvas do.
- The backend prevents no key's default action in the host. The
  elements there are the program's adapter's, whose own listener runs
  first and prevents or stops what it wants.
- A key in a field in the host (an input, text area, select or editable
  element) comes without `mwin_eventTextInput`: the field keeps its
  typed text, which the adapter reads. Elsewhere in the host a typed
  character comes as on the canvas.
- `mwinRequestFocus` with the focus already within the window leaves it
  where it is and answers done.

## Consequences

A screen reader user keeps the program's keys and the window's focus
while moving among its ARIA elements, native controls in the host keep
their behavior, and keys on the page's own content stay the page's.
`test_web_host` checks it in headless Chrome.
