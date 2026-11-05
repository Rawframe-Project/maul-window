#!/bin/sh
# Runs a test application in the Android emulator: installs
# it on the device ANDROID_SERIAL names (else the only one), starts its
# activity, and waits, a minute at most, for the closing line its quit
# writes to files/out, "result: N failures", read with run-as (the
# application is debuggable). A line "adb: <command>" the test writes is
# run once, as "adb shell <command>", and a line "emu: <command>" as
# "adb emu <command>" (the emulator's console, whose touches go through
# the system's input filters as a finger's do), so that the test drives
# what only the system can do (the home key, a rotation, touch
# exploration); the rotation settings
# are put back at the end, whatever the test did. The system's
# animations are off while it runs, as for any UI test, so that an
# opening transition does not move the window under the test's input.
# The night mode, the font scale, battery saver, the battery's state and
# the enabled accessibility services, which a test may change, are put
# back too, and a finger the console put down is lifted. The library never ends
# the process, so the runner stops it. Passes when the line says 0
# failures and the system did not restart meanwhile; otherwise shows the
# application's crashes from the log, the Java exceptions the library
# described there, what the clipboard service said, each ANR whole, the
# activity made anew or input dropped, and the resources' changes, and a
# restart of the system with Android's own abort message, apart from the
# test's failures. With a third argument, a failed run's whole log since
# the start, every buffer, is saved to that file, and a passed run
# removes it.
#
# adb can lose its connection to the device ("error: closed"). The steps
# that only read or set the device's state, install or start the
# application are tried up to three times. An application an earlier
# run left is removed before the install, and the result file before the
# start, so a result an earlier run left cannot be read as this one's.
# A command the test asks for is not tried again, and the test fails
# when adb loses it. The device is put back however the script ends,
# without changing its status; a setting whose read stayed lost is left
# as it is, rather than set to nothing.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2
log=${3:-}

# An idempotent adb step, tried up to three times while adb fails.
again() {
    tries=1
    until "$adb" "$@"; do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
}

# An idempotent adb step's output, tried up to three times while adb
# fails: only the try that succeeded is kept, so an answer a lost try
# printed before its connection dropped is not given twice.
ask() {
    tries=1
    until output=$("$adb" "$@"); do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
    printf '%s\n' "$output"
}

# A setting read through ask, or "lost" when the read stayed lost.
setting() {
    if value=$(ask shell "$@"); then
        printf '%s\n' "$value" | tr -d '\r'
    else
        echo lost
    fi
}

# Puts a setting back to the value read, appended to the command, unless
# its read stayed lost.
# shellcheck disable=SC2317 # called by restore
put_back() {
    value=$1
    shift
    [ "$value" = lost ] || again shell "$@" "$value" > /dev/null
}

# Puts the device back as the run found it, whatever ended the run.
# shellcheck disable=SC2317 # called by the EXIT trap
restore() {
    set +e
    again shell am force-stop "$package"
    # A finger the console left down, by a test that ended early, is lifted.
    if [ -n "$out" ] && grep -q '^emu: event mouse' "$out"; then
        again emu event mouse 0 0 0 0 > /dev/null
    fi
    # A new device has neither set: the defaults are put back.
    [ "$rotation" = null ] && rotation=0
    [ "$rotating" = null ] && rotating=1
    put_back "$rotation" settings put system user_rotation
    put_back "$rotating" settings put system accelerometer_rotation
    for scale in window_animation_scale transition_animation_scale animator_duration_scale; do
        eval "value=\$old_$scale"
        [ "$value" = null ] && value=1
        put_back "$value" settings put global "$scale"
    done
    put_back "$night" cmd uimode night
    [ "$font" = null ] && font=1.0
    put_back "$font" settings put system font_scale
    [ "$saver" = null ] && saver=0
    put_back "$saver" settings put global low_power
    again shell dumpsys battery reset
    if [ "$services" = null ] || [ -z "$services" ]; then
        again shell settings delete secure enabled_accessibility_services > /dev/null
    else
        put_back "$services" settings put secure enabled_accessibility_services
    fi
    [ "$accessible" = null ] && accessible=0
    put_back "$accessible" settings put secure accessibility_enabled
    again uninstall "$package" > /dev/null
    [ -z "$out" ] || rm -f "$out"
}
# A few seconds after a fresh boot, SystemUI takes its theme's overlays
# off, and at times puts them back (Android 12 and later): each a change
# of the resources that makes the activity in front anew, so that input
# a test asked for meanwhile meets no window. On a fresh boot (the
# overlays on, the resources never changed) the test starts once they
# have changed and then stayed so for five seconds, or after forty
# seconds anyway, saying whether they never changed or kept changing.
assets() {
    "$adb" shell dumpsys window | grep -m1 mGlobalConfiguration | grep -oE 'as\.[0-9]+' || true
}
if "$adb" shell cmd overlay list | grep -qE '\[x\] com\.android\.systemui:(neutral|accent|dynamic)' &&
    [ -z "$(assets)" ]; then
    began=$(date +%s)
    seen=
    since=$began
    settled=
    while [ $(($(date +%s) - began)) -lt 40 ]; do
        now=$(assets)
        if [ "$now" != "$seen" ]; then
            seen=$now
            since=$(date +%s)
        elif [ -n "$now" ] && [ $(($(date +%s) - since)) -ge 5 ]; then
            settled=1
            break
        fi
        sleep 0.2
    done
    if [ -n "$settled" ]; then
        echo "theme settled at $seen in $(($(date +%s) - began)) s"
    elif [ -z "$seen" ]; then
        echo "theme never changed in 40 s; starting anyway"
    else
        echo "theme still changing after 40 s; starting anyway"
    fi
fi
start=$(date +%s)
out=
rotating=lost rotation=lost night=lost font=lost saver=lost services=lost accessible=lost
# shellcheck disable=SC2034 # read through eval
old_window_animation_scale=lost old_transition_animation_scale=lost old_animator_duration_scale=lost
trap restore EXIT
trap 'exit 1' HUP INT TERM
rotating=$(setting settings get system accelerometer_rotation)
rotation=$(setting settings get system user_rotation)
for scale in window_animation_scale transition_animation_scale animator_duration_scale; do
    eval "old_$scale=\$(setting settings get global $scale)"
    again shell settings put global "$scale" 0
done
night=$(setting cmd uimode night | sed 's/^Night mode: //')
font=$(setting settings get system font_scale)
saver=$(setting settings get global low_power)
services=$(setting settings get secure enabled_accessibility_services)
accessible=$(setting settings get secure accessibility_enabled)
# Each run starts with the application fresh: one an earlier run left,
# as when its uninstall was lost, is removed with its data first. The
# media provider handles a package's removal in its own time, and orphans
# the documents the package owns then, the ones a test made since among
# them. So the runner waits, 60 s at most, until the system has delivered
# its broadcasts, the removal's and the install's, before the test starts.
# Each answer is read whole first: a lost read ends the run here (set -e),
# where a pipe would give its last command's status and an empty answer.
installed=$(ask shell pm list packages "$package")
if printf '%s\n' "$installed" | tr -d '\r' | grep -qx "package:$package"; then
    again uninstall "$package" > /dev/null
fi
again install -r "$apk" > /dev/null
if ! timeout 60 "$adb" shell am wait-for-broadcast-idle > /dev/null; then
    echo "the system's broadcasts were not seen idle in 60 s"
fi
# Should the removal have been lost too, install -r keeps the data,
# files/out among it. The result file is removed by name, and the device
# asked whether it is gone: anything but "absent" (run-as failing among
# it) fails the test, whose result could be the earlier one.
again shell run-as "$package" rm -f files/out || true
gone=$(ask shell "run-as $package sh -c 'if [ -e files/out ]; then echo present; else echo absent; fi'")
gone=$(printf '%s' "$gone" | tr -d '\r')
if [ "$gone" != absent ]; then
    echo "an earlier run's result may be left: the device answered '$gone'"
fi
began_assets=$(assets)
server=$(ask shell pidof system_server)
server=$(printf '%s' "$server" | tr -d '\r')
again logcat -b all -c
# The minute for the result counts from the start, whatever the steps
# before it took.
launched=$(date +%s)
again shell am start -W -n "$package/maul.window.Activity" > /dev/null
echo "started $package in $(($(date +%s) - start)) s"
out=$(mktemp)
done=0
lost=
while [ $(($(date +%s) - launched)) -lt 60 ]; do
    "$adb" shell run-as "$package" cat files/out > "$out" 2> /dev/null || true
    commands=$(grep -E '^(adb|emu): ' "$out" || true)
    count=$(printf '%s' "$commands" | grep -c '' || true)
    while [ "$done" -lt "$count" ]; do
        done=$((done + 1))
        line=$(printf '%s\n' "$commands" | sed -n "${done}p")
        command=${line#*: }
        echo "running: $line"
        case $line in
            emu:*) set -- emu $command ;;
            *) set -- shell "$command" ;;
        esac
        # Not tried again, since it need not be idempotent: the test did
        # not get what it asked for, so it fails.
        if ! "$adb" "$@" > /dev/null; then
            lost=$line
            echo "adb lost the device running: $line"
        fi
    done
    grep -q '^result: ' "$out" && break
    sleep 0.1
done
echo "waited until $(($(date +%s) - launched)) s after the start"
grep -v -E '^(adb|emu): ' "$out" || true
# The system's crash takes the test with it: said as such, once the
# system is back.
# A pid that cannot be read, the system down or adb lost, fails the test
# as a restart would.
if server_now=$(ask shell pidof system_server); then
    server_now=$(printf '%s' "$server_now" | tr -d '\r')
else
    server_now=
fi
restarted=
if [ "$server_now" != "$server" ]; then
    restarted=1
    echo "the system restarted during the test: system_server was pid $server, now ${server_now:-not read}"
    "$adb" logcat -d -b crash | grep -E 'Fatal signal|Abort message' | head -6 || true
    began=$(date +%s)
    while [ $(($(date +%s) - began)) -lt 60 ] &&
        ! "$adb" shell service check activity 2> /dev/null | grep -q ': found'; do
        sleep 1
    done
fi
status=1
if [ -z "$restarted" ] && [ -z "$lost" ] && [ "$gone" = absent ] &&
    grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E libc:F ActivityManager:W | tail -60
    # Each native crash's tombstone whole, from its header on: the signal,
    # the fault address and the top frames come first, which a tail cuts.
    "$adb" logcat -d -s DEBUG:F | sed -n '/\*\*\* \*\*\* \*\*\*/,$p'
    # A Java exception the library's helpers threw (described to the log
    # as System.err), what the clipboard service said, and each ANR's
    # block whole, from its header on, which the tail above can cut.
    "$adb" logcat -d -s System.err:W ClipboardService:V | tail -80
    "$adb" logcat -d -s ActivityManager:E | sed -n '/ANR in /,$p'
    # Whether the system made the activity anew or dropped input meant
    # for it, and the resources' changes from the start to now.
    "$adb" logcat -d -s ActivityTaskManager:I WindowManager:I InputDispatcher:W |
        grep -E 'Config changes|relaunch|NO_INPUT_CHANNEL|no input channel' | tail -20 || true
    echo "assets from ${began_assets:-as.0} to $(assets | grep . || echo as.0)"
    if [ -n "$log" ]; then
        "$adb" logcat -d -b all -v threadtime > "$log" || true
        echo "the whole log: $log"
    fi
fi
if [ "$status" = 0 ] && [ -n "$log" ]; then
    rm -f "$log"
fi
# restore puts the device back as the script exits; the status stands.
exit $status
