#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The display fixture signals only the processes it started. Its record
# is planted with two processes of this test's own under PIDs it does
# not own: one recorded with a stale start time, as when the kernel gave
# the PID of a server that went to another process, and one with its
# start time but another command line. Start and stop must leave both
# alone, drop them from the record, and still run and stop their own
# Xvfb; a second start keeps that Xvfb.
#
#   test/test_display_fixture.sh FIXTURE RUNTIME DISPLAY
set -euo pipefail

fixture=$1
runtime=$2
number=$3
pids=$runtime/display.pids
failures=0

Fail() {
    echo "FAIL: $1"
    failures=$((failures + 1))
}

StartTime() {
    local stat
    stat=$(cat "/proc/$1/stat")
    stat=${stat##*) }
    echo "$stat" | cut -d ' ' -f 20
}

Alive() {
    kill -0 "$1" 2>/dev/null
}

# Plants the two processes in the record.
Plant() {
    echo "$stale 1 Xvfb :$number " >>"$pids"
    echo "$other $(StartTime "$other") Xvfb :$number " >>"$pids"
}

rm -rf "$runtime"
mkdir -p "$runtime"
sleep 600 &
stale=$!
sleep 600 &
other=$!
trap 'kill "$stale" "$other" 2>/dev/null || true; "$fixture" stop "$runtime" || true' EXIT

Plant
"$fixture" start "$runtime" "$number" || Fail "start with a planted record"
Alive "$stale" || Fail "start signalled a PID recorded with a stale start time"
Alive "$other" || Fail "start signalled a PID recorded with another command line"
if grep -q -E "^($stale|$other) " "$pids"; then
    Fail "start kept the planted PIDs in its record"
fi
server=$(head -n 1 "$pids" | cut -d ' ' -f 1)
Alive "$server" || Fail "start runs no Xvfb"

"$fixture" start "$runtime" "$number" || Fail "a second start"
[ "$(head -n 1 "$pids" | cut -d ' ' -f 1)" = "$server" ] || Fail "a second start replaced the Xvfb"

Plant
"$fixture" stop "$runtime" || Fail "stop"
Alive "$stale" || Fail "stop signalled a PID recorded with a stale start time"
Alive "$other" || Fail "stop signalled a PID recorded with another command line"
for _ in $(seq 50); do
    Alive "$server" || break
    sleep 0.1
done
Alive "$server" && Fail "stop left the Xvfb running"
[ -e "$pids" ] && Fail "stop kept its record"

[ "$failures" = 0 ]
