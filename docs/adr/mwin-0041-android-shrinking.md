# mwin-0041. What an Android application keeps when it shrinks its code

Status: Accepted

## Context

On Android the application compiles the library's Java with its own
(`java/maul/window`). The native library loads those classes by name,
calls their members by name and signature through JNI and registers its
native methods on them; since 0.12.0 it also finds a public
`int virtualViewAt(float x, float y)` on an accessibility root that
cannot implement `maul.window.Explorer` (a provider of another library,
such as Maul UI's) by reflection. A release build with R8 or ProGuard
renames and removes whatever its rules do not keep; R8 sees neither JNI
nor a reflective lookup on a class it cannot know, so such an
application broke at its first JNI call, and a root's method lost its
name. Nothing said what to keep, and the test applications, built with
d8 alone, could not show it.

## Decision

- The library ships `java/proguard-rules.pro`: every class of the
  `maul.window` package kept whole, and `public int virtualViewAt(float,
  float)` kept in every class. An application that shrinks its code adds
  the file to its rules; the guide says so.
- The test applications are built by R8 (shrinking, optimizing and
  renaming, as a release build) with that file, the manifest's rules
  from aapt2 and the tests' own (`test/android/proguard-rules.pro`), so
  every Android test run checks the library's rules. The tests' root
  without Explorer (`PlainTree`) is no Explorer's superclass, so only
  the library's rule keeps its method: built without that rule, the
  accessibility test fails.
- A root's method is found by its name and signature, `int
  virtualViewAt(float, float)`, with the meaning of Explorer's: the
  virtual view under a place in the view's coordinates, or `View.NO_ID`.

## Consequences

Shrunk applications keep working, and a change that reaches a new Java
member by name without keeping it fails the Android tests. The rules
keep the library's few classes whole rather than member by member,
which costs an application a few kilobytes and cannot miss one.
