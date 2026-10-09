# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The tests' own Java, found by name from their native code: the
# accessibility client's static members, the trees' constructors and
# Tree's count of reads. PlainTree's virtualViewAt is left to the
# library's rules (java/proguard-rules.pro), which the test of a root
# without Explorer thereby checks.
-keep class maul.window.tests.Client { *; }
-keep class maul.window.tests.Tree { <init>(...); static int read; }
-keep class maul.window.tests.ExplorerTree { <init>(...); }
-keep class maul.window.tests.PlainTree { <init>(...); }
