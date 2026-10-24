#!/bin/sh
# Prints the UDID of the first available simulator whose name starts
# with KIND (iPhone, iPad) in the newest iOS runtime, for CI; fails,
# saying so, when that runtime has none.
#
#   tools/ci_simulator.sh KIND
set -eu
xcrun simctl list devices available -j | python3 -c '
import json, sys
kind = sys.argv[1]
devices = json.load(sys.stdin)["devices"]
runtimes = sorted((r for r in devices if ".iOS-" in r),
                  key=lambda r: [int(p) for p in r.rsplit("iOS-", 1)[1].split("-")])
if not runtimes:
    sys.exit("no iOS runtime has an available simulator")
found = [d["udid"] for d in devices[runtimes[-1]] if d["name"].startswith(kind)]
if not found:
    sys.exit("no available " + kind + " simulator in " + runtimes[-1])
print(found[0])' "$1"
