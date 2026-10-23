#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The display fixture of the Linux tests: an Xvfb, and a headless weston
# where the Wayland backend is built, private to one build tree.
#
#   tools/test_display.sh start RUNTIME DISPLAY [wayland]
#   tools/test_display.sh stop RUNTIME
#
# RUNTIME is the tests' XDG_RUNTIME_DIR, where weston's socket
# (wayland-mwin), the logs and the PIDs go; DISPLAY is the X display
# number. ctest runs start before the tests that need a display and stop
# after them (CMakeLists.txt); a repeated run (ctest --repeat) runs start
# again, which keeps the servers it started while they run. Stop kills
# only the PIDs start recorded, never a process found by name: other
# builds and sessions run their own.
set -euo pipefail

action=$1
runtime=$2
pids=$runtime/display.pids

# Whether Xvfb, and weston where asked, take clients.
Ready() {
    [ -s "$runtime/xvfb.ready" ] || return 1
    [ "${1:-}" != wayland ] || [ -S "$runtime/wayland-mwin" ]
}

# Whether every process start recorded runs, and they take clients.
Running() {
    [ -s "$pids" ] || return 1
    while read -r pid; do
        kill -0 "$pid" 2>/dev/null || return 1
    done <"$pids"
    Ready "$@"
}

# Stops the processes start recorded, and waits until they are gone and
# their display is free.
Stop() {
    [ -f "$pids" ] || return 0
    while read -r pid; do
        kill "$pid" 2>/dev/null || true
    done <"$pids"
    for _ in $(seq 50); do
        alive=0
        while read -r pid; do
            if kill -0 "$pid" 2>/dev/null; then alive=1; fi
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
    Xvfb ":$number" -screen 0 1280x800x24 -nolisten tcp -noreset -displayfd 3 \
        3>"$runtime/xvfb.ready" </dev/null >"$runtime/xvfb.log" 2>&1 &
    echo $! >>"$pids"
    if [ "${4:-}" = wayland ]; then
        XDG_RUNTIME_DIR=$runtime weston --backend=headless --socket=wayland-mwin --idle-time=0 \
            </dev/null >"$runtime/weston.log" 2>&1 &
        echo $! >>"$pids"
    fi
    for _ in $(seq 100); do
        Ready "${4:-}" && exit 0
        sleep 0.1
    done
    echo "the display fixture did not start; its logs:" >&2
    tail -n 20 "$runtime"/*.log >&2 || true
    exit 1
    ;;
stop)
    Stop
    ;;
*)
    echo "usage: tools/test_display.sh start RUNTIME DISPLAY [wayland] | stop RUNTIME" >&2
    exit 2
    ;;
esac
