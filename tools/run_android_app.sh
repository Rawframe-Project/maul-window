#!/bin/sh
# Runs a test application in the Android emulator (mwin-0026): installs
# it on the device ANDROID_SERIAL names (else the only one), starts its
# activity, and waits, a minute at most, for the closing line its quit
# writes to files/out, "result: N failures", read with run-as (the
# application is debuggable). A line "adb: <command>" the test writes is
# run once, as "adb shell <command>", so that the test drives what only
# the system can do (the home key, a rotation); the rotation settings
# are put back at the end, whatever the test did. The library never ends
# the process, so the runner stops it. Passes when the line says 0
# failures; otherwise shows the application's crashes from the log.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2
start=$(date +%s)
rotating=$("$adb" shell settings get system accelerometer_rotation | tr -d '\r')
rotation=$("$adb" shell settings get system user_rotation | tr -d '\r')
"$adb" install -r "$apk" > /dev/null
"$adb" logcat -c
"$adb" shell am start -W -n "$package/maul.window.Activity" > /dev/null
echo "started $package in $(($(date +%s) - start)) s"
out=$(mktemp)
done=0
while [ $(($(date +%s) - start)) -lt 60 ]; do
    "$adb" shell run-as "$package" cat files/out > "$out" 2> /dev/null || true
    commands=$(grep '^adb: ' "$out" || true)
    count=$(printf '%s' "$commands" | grep -c '' || true)
    while [ "$done" -lt "$count" ]; do
        done=$((done + 1))
        command=$(printf '%s\n' "$commands" | sed -n "${done}p" | sed 's/^adb: //')
        echo "running: $command"
        "$adb" shell "$command" > /dev/null
    done
    grep -q '^result: ' "$out" && break
    sleep 0.5
done
echo "waited until $(($(date +%s) - start)) s"
grep -v '^adb: ' "$out" || true
status=1
if grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E DEBUG:F libc:F ActivityManager:W | tail -60
fi
"$adb" shell am force-stop "$package"
"$adb" shell settings put system user_rotation "$rotation"
"$adb" shell settings put system accelerometer_rotation "$rotating"
"$adb" uninstall "$package" > /dev/null
rm -f "$out"
exit $status
