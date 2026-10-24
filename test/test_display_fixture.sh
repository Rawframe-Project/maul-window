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
# A run killed outright never runs stop: start's watchdog must then stop
# the servers and go itself, within a bounded time. A ctest-like parent
# runs start and is killed with SIGKILL; start's processes must keep
# none of that parent's descriptors.
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

# Whether a process runs: a zombie does not. A container whose first
# process reaps no orphans, as CI's job containers, keeps a stopped
# server a zombie.
Alive() {
    local stat
    stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    stat=${stat##*) }
    [ "${stat%% *}" != Z ]
}

# Whether the process that started at START under PID is gone.
Gone() {
    ! Alive "$1" || [ "$(StartTime "$1" 2>/dev/null)" != "$2" ]
}

# Prints the first descriptor of the held ones that PID keeps and
# succeeds, or prints nothing and fails.
Kept() {
    local fd target kept
    for fd in /proc/"$1"/fd/*; do
        target=$(readlink "$fd" 2>/dev/null) || continue
        for kept in "${held[@]}"; do
            if [ "$target" = "$kept" ]; then
                echo "$kept"
                return 0
            fi
        done
    done
    return 1
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

watchdog=$(grep " bash $fixture watch " "$pids" | cut -d ' ' -f 1,2) || true
[ -n "$watchdog" ] || Fail "start left no watchdog"

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
# shellcheck disable=SC2086 # the PID and its start time
[ -n "$watchdog" ] && ! Gone $watchdog && Fail "stop left the watchdog running"

# What start's processes must not keep of their parent's: a file only
# the parent opened, and the parent's output where it is a pipe, which
# ctest reads to its end.
exec 9>"$runtime/inherited"
held=("$(readlink -f "$runtime/inherited")")
for fd in 1 2; do
    target=$(readlink "/proc/$$/fd/$fd")
    [ "${target#pipe:}" != "$target" ] && held+=("$target")
done
bash -c '"$0" start "$1" "$2" && exec sleep 600' "$fixture" "$runtime" "$number" &
parent=$!
exec 9>&-
for _ in $(seq 100); do
    line=$(tr '\0' ' ' <"/proc/$parent/cmdline" 2>/dev/null) || break
    [ "${line#sleep }" != "$line" ] && break
    sleep 0.1
done
if [ "${line#sleep }" = "$line" ]; then
    Fail "start under a ctest-like parent"
else
    mapfile -t recorded < <(cut -d ' ' -f 1,2 "$pids")
    [ "${#recorded[@]}" = 2 ] || Fail "start recorded ${#recorded[@]} processes, not an Xvfb and a watchdog"
    # The watchdog closes them once its bash runs, which nothing orders
    # before the parent's exec: each process gets a second.
    for process in "${recorded[@]}"; do
        for _ in $(seq 10); do
            kept=$(Kept "${process%% *}") || break
            sleep 0.1
        done
        [ -n "$kept" ] && Fail "process ${process%% *} keeps its parent's $kept"
    done
    # The shell's notice of the killed job goes to the discarded stderr.
    { kill -9 "$parent" && wait "$parent"; } 2>/dev/null || true
    for _ in $(seq 100); do
        left=0
        for process in "${recorded[@]}"; do
            # shellcheck disable=SC2086 # the PID and its start time
            Gone $process || left=$((left + 1))
        done
        [ "$left" = 0 ] && break
        sleep 0.1
    done
    [ "$left" = 0 ] || Fail "$left processes of a killed run still run after 10 s"
    [ -e "$pids" ] && Fail "the watchdog kept the record of a killed run"
fi

[ "$failures" = 0 ]
