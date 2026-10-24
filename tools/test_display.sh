#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The display fixture of the Linux tests: an Xvfb, and a headless weston
# where the Wayland backend is built, private to one build tree.
#
#   tools/test_display.sh start RUNTIME DISPLAY [wayland]
#   tools/test_display.sh stop RUNTIME
#   tools/test_display.sh watch RUNTIME PARENT START   (start's own)
#
# RUNTIME is the tests' XDG_RUNTIME_DIR, where weston's socket
# (wayland-mwin), the logs and the PIDs go; DISPLAY is the X display
# number. ctest runs start before the tests that need a display and stop
# after them (CMakeLists.txt); a repeated run (ctest --repeat) runs start
# again, which keeps the servers it started while they run.
#
# Start also leaves a watchdog (this script's watch action), since a
# ctest run killed or cancelled never runs stop: once that run, start's
# parent by PID and start time, is gone, the watchdog stops the servers.
# It goes itself once stop has run or a new start replaced it. The
# watchdog and the servers close every descriptor they inherit, so none
# holds ctest's pipes or a lock of whoever runs it.
#
# Start records each process it starts as its PID, its start time
# (field 22 of /proc/PID/stat) and the start of its command line. A
# recorded PID is the fixture's only while all three still match: the
# record outlives reboots and cancelled runs, and the kernel gives a PID
# that went to another process, maybe another session's. Stop and a new
# start signal only processes that match their record, and drop the
# others unsignalled. Nothing is ever found by name.
set -euo pipefail

action=$1
runtime=$2
pids=$runtime/display.pids

# A process's start time, in clock ticks after boot, or nothing.
StartTime() {
    local stat
    stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 0
    # The fields after the command name, which may hold spaces, start at 3.
    stat=${stat##*) }
    echo "$stat" | cut -d ' ' -f 20
}

# Whether a process runs, as the one that started at START: a zombie
# does not.
Runs() {
    local stat
    stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    stat=${stat##*) }
    [ "${stat%% *}" != Z ] && [ "$(StartTime "$1")" = "$2" ]
}

# Closes every descriptor this process inherited but the standard three
# and bash's hold on the script (255).
CloseInherited() {
    local fd
    for fd in /proc/"$BASHPID"/fd/*; do
        fd=${fd##*/}
        if [ "$fd" -gt 2 ] && [ "$fd" != 255 ]; then
            eval "exec $fd>&-"
        fi
    done
}

# Whether PID is the process a record names: the same start time and a
# command line starting with COMMAND.
Matches() {
    local pid=$1 start=$2 command=$3 line
    [ -n "$start" ] && [ "$(StartTime "$pid")" = "$start" ] || return 1
    line=$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null) || return 1
    [ "${line#"$command"}" != "$line" ]
}

# Records the process just started, by PID and command.
Record() {
    echo "$1 $(StartTime "$1") $2" >>"$pids"
}

# Whether Xvfb, and weston where asked, take clients.
Ready() {
    [ -s "$runtime/xvfb.ready" ] || return 1
    [ "${1:-}" != wayland ] || [ -S "$runtime/wayland-mwin" ]
}

# Whether every recorded process is still the fixture's, and they take
# clients.
Running() {
    [ -s "$pids" ] || return 1
    while read -r pid start command; do
        Matches "$pid" "$start" "$command" || return 1
    done <"$pids"
    Ready "$@"
}

# Stops the recorded processes that are still the fixture's, waits until
# they are gone, and forgets the record.
Stop() {
    [ -f "$pids" ] || return 0
    # The watchdog stopping the servers never signals itself.
    while read -r pid start command; do
        if [ "$pid" != $$ ] && Matches "$pid" "$start" "$command"; then
            kill "$pid" 2>/dev/null || true
        fi
    done <"$pids"
    for _ in $(seq 50); do
        alive=0
        while read -r pid start command; do
            if [ "$pid" != $$ ] && Matches "$pid" "$start" "$command"; then alive=1; fi
        done <"$pids"
        [ "$alive" = 0 ] && break
        sleep 0.1
    done
    rm -f "$pids"
}

case "$action" in
start)
    number=$3
    mkdir -p "$runtime"
    chmod 700 "$runtime"
    if Running "${4:-}"; then
        exit 0
    fi
    Stop
    : >"$pids"
    # Detached from ctest's pipes, which ctest would otherwise wait on.
    # Without -noreset the server resets whenever its last client goes,
    # reloading its keymap, and a test connecting meanwhile fails: the
    # tests come and go in parallel. It writes its display number to
    # descriptor 3 once it takes clients, some 40 ms after its socket
    # file appears.
    rm -f "$runtime/xvfb.ready"
    (
        CloseInherited
        exec Xvfb ":$number" -screen 0 1280x800x24 -nolisten tcp -noreset -displayfd 3 \
            3>"$runtime/xvfb.ready" </dev/null >"$runtime/xvfb.log" 2>&1
    ) &
    Record $! "Xvfb :$number "
    if [ "${4:-}" = wayland ]; then
        (
            CloseInherited
            XDG_RUNTIME_DIR=$runtime exec weston --backend=headless --socket=wayland-mwin \
                --idle-time=0 </dev/null >"$runtime/weston.log" 2>&1
        ) &
        Record $! "weston --backend=headless --socket=wayland-mwin "
    fi
    for _ in $(seq 100); do
        if Ready "${4:-}"; then
            bash "$0" watch "$runtime" "$PPID" "$(StartTime "$PPID")" </dev/null >/dev/null 2>&1 &
            Record $! "bash $0 watch $runtime "
            exit 0
        fi
        sleep 0.1
    done
    echo "the display fixture did not start; its logs:" >&2
    tail -n 20 "$runtime"/*.log >&2 || true
    exit 1
    ;;
stop)
    Stop
    ;;
watch)
    parent=$3
    started=$4
    CloseInherited
    while sleep 1; do
        # Stop ran, or a new start replaced this watchdog.
        grep -q "^$$ " "$pids" 2>/dev/null || exit 0
        if ! Runs "$parent" "$started"; then
            Stop
            exit 0
        fi
    done
    ;;
*)
    echo "usage: tools/test_display.sh start RUNTIME DISPLAY [wayland] | stop RUNTIME" >&2
    exit 2
    ;;
esac
