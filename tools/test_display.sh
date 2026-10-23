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
# after them (CMakeLists.txt). Stop kills only the PIDs start recorded,
# never a process found by name: other builds and sessions run their own.
set -euo pipefail

action=$1
runtime=$2
pids=$runtime/display.pids

case "$action" in
start)
    number=$3
    mkdir -p "$runtime"
    chmod 700 "$runtime"
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
        ready=1
        [ -s "$runtime/xvfb.ready" ] || ready=0
        if [ "${4:-}" = wayland ] && [ ! -S "$runtime/wayland-mwin" ]; then ready=0; fi
        [ "$ready" = 1 ] && exit 0
        sleep 0.1
    done
    echo "the display fixture did not start; its logs:" >&2
    tail -20 "$runtime"/*.log >&2 || true
    exit 1
    ;;
stop)
    if [ -f "$pids" ]; then
        while read -r pid; do
            kill "$pid" 2>/dev/null || true
        done <"$pids"
        rm -f "$pids"
    fi
    ;;
*)
    echo "usage: tools/test_display.sh start RUNTIME DISPLAY [wayland] | stop RUNTIME" >&2
    exit 2
    ;;
esac
