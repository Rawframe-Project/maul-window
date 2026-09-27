# mwin-0003. Maul Unicode as a dependency

Status: Accepted

## Context

Every string that crosses the library (titles now; text input, IME,
the clipboard, dropped paths later) must be validated as bounded
UTF-8, and Maul Unicode's validator is the family's. Other Maul
libraries are allowed dependencies, pinned by release; nothing is
vendored.

## Decision

The build uses an installed `maul-unicode` package of the pinned
version (0.2.0) when `find_package` finds one, and otherwise fetches
that release's tag and builds it along, installing it with this library
so the installed package config can `find_dependency` it. It links
privately: no public header of this library includes Maul Unicode's.

## Consequences

A consumer with Maul Unicode installed builds offline; one without it
needs network access at configure time once. A new Maul Unicode
release reaches this library only by changing the pinned version.
