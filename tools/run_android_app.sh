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
# failures; otherwise shows the application's crashes from the log, the
# activity made anew or input dropped, and the resources' changes.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2
# A few seconds after a fresh boot, SystemUI takes its theme's overlays
# off, and at times puts them back (Android 12 and later): each a change
# of the resources that makes the activity in front anew, so that input
# a test asked for meanwhile meets no window. On a fresh boot (the
# overlays on, the resources never changed) the test starts once they
# have changed and then stayed so for five seconds, forty at most.
assets() {
    "$adb" shell dumpsys window | grep -m1 mGlobalConfiguration | grep -oE 'as\.[0-9]+' || true
}
if "$adb" shell cmd overlay list | grep -qE '\[x\] com\.android\.systemui:(neutral|accent|dynamic)' &&
    [ -z "$(assets)" ]; then
    began=$(date +%s)
    seen=
    since=$began
    while [ $(($(date +%s) - began)) -lt 40 ]; do
        now=$(assets)
        if [ "$now" != "$seen" ]; then
            seen=$now
            since=$(date +%s)
        elif [ -n "$now" ] && [ $(($(date +%s) - since)) -ge 5 ]; then
            break
        fi
        sleep 0.2
    done
    echo "theme settled at ${seen:-no change} in $(($(date +%s) - began)) s"
fi
start=$(date +%s)
rotating=$("$adb" shell settings get system accelerometer_rotation | tr -d '\r')
rotation=$("$adb" shell settings get system user_rotation | tr -d '\r')
for scale in window_animation_scale transition_animation_scale animator_duration_scale; do
    eval "old_$scale=\$(\"\$adb\" shell settings get global $scale | tr -d '\\r')"
    "$adb" shell settings put global "$scale" 0
done
night=$("$adb" shell cmd uimode night | tr -d '\r' | sed 's/^Night mode: //')
font=$("$adb" shell settings get system font_scale | tr -d '\r')
saver=$("$adb" shell settings get global low_power | tr -d '\r')
services=$("$adb" shell settings get secure enabled_accessibility_services | tr -d '\r')
accessible=$("$adb" shell settings get secure accessibility_enabled | tr -d '\r')
"$adb" install -r "$apk" > /dev/null
began_assets=$(assets)
"$adb" logcat -c
"$adb" shell am start -W -n "$package/maul.window.Activity" > /dev/null
echo "started $package in $(($(date +%s) - start)) s"
out=$(mktemp)
done=0
while [ $(($(date +%s) - start)) -lt 60 ]; do
    "$adb" shell run-as "$package" cat files/out > "$out" 2> /dev/null || true
    commands=$(grep -E '^(adb|emu): ' "$out" || true)
    count=$(printf '%s' "$commands" | grep -c '' || true)
    while [ "$done" -lt "$count" ]; do
        done=$((done + 1))
        line=$(printf '%s\n' "$commands" | sed -n "${done}p")
        command=${line#*: }
        echo "running: $line"
        case $line in
            emu:*) "$adb" emu $command > /dev/null ;;
            *) "$adb" shell "$command" > /dev/null ;;
        esac
    done
    grep -q '^result: ' "$out" && break
    sleep 0.1
done
echo "waited until $(($(date +%s) - start)) s"
grep -v -E '^(adb|emu): ' "$out" || true
status=1
if grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E DEBUG:F libc:F ActivityManager:W | tail -60
    # Whether the system made the activity anew or dropped input meant
    # for it, and the resources' changes from the start to now.
    "$adb" logcat -d -s ActivityTaskManager:I WindowManager:I InputDispatcher:W |
        grep -E 'Config changes|relaunch|NO_INPUT_CHANNEL|no input channel' | tail -20 || true
    echo "assets from ${began_assets:-as.0} to $(assets | grep . || echo as.0)"
fi
"$adb" shell am force-stop "$package"
# A finger the console left down, by a test that ended early, is lifted.
if grep -q '^emu: event mouse' "$out"; then
    "$adb" emu event mouse 0 0 0 0 > /dev/null
fi
# A new device has neither set: the defaults are put back.
[ "$rotation" = null ] && rotation=0
[ "$rotating" = null ] && rotating=1
"$adb" shell settings put system user_rotation "$rotation"
"$adb" shell settings put system accelerometer_rotation "$rotating"
for scale in window_animation_scale transition_animation_scale animator_duration_scale; do
    eval "value=\$old_$scale"
    [ "$value" = null ] && value=1
    "$adb" shell settings put global "$scale" "$value"
done
"$adb" shell cmd uimode night "$night" > /dev/null
[ "$font" = null ] && font=1.0
"$adb" shell settings put system font_scale "$font"
[ "$saver" = null ] && saver=0
"$adb" shell settings put global low_power "$saver"
"$adb" shell dumpsys battery reset
if [ "$services" = null ] || [ -z "$services" ]; then
    "$adb" shell settings delete secure enabled_accessibility_services > /dev/null
else
    "$adb" shell settings put secure enabled_accessibility_services "$services"
fi
[ "$accessible" = null ] && accessible=0
"$adb" shell settings put secure accessibility_enabled "$accessible"
"$adb" uninstall "$package" > /dev/null
rm -f "$out"
exit $status
