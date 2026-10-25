#!/bin/sh
# Runs a test application in the iOS simulator: installs the
# bundle on the booted device (or MWIN_IOS_DEVICE), launches it with its
# output in a file, and waits, two minutes at most, for the closing line
# its quit prints, "result: N failures". simctl launch returns once the
# application runs, not when it ends, and the library never ends the
# process, so the runner ends it. A line "simctl: <verb> <arguments>"
# the test writes is run once, as "xcrun simctl <verb> <device>
# <arguments>", so that the test drives what only the system can do
# (another application in front of it, and its own brought back). Passes
# when the line says 0 failures; otherwise shows the application's
# errors, and where it is stuck or why it ended.
set -eu
app=$1
device=${MWIN_IOS_DEVICE:-booted}
name=$(basename "$app" .app)
id=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$app/Info.plist")
out=$(mktemp -t mwin-ios-out)
err=$(mktemp -t mwin-ios-err)
start=$(date +%s)
xcrun simctl install "$device" "$app"
echo "installed $id in $(($(date +%s) - start)) s"
# simctl says "<id>: <pid>"; the simulator's processes are the host's.
# The application writes its output where MWIN_TEST_OUT names, simctl's
# own redirection giving nothing on a CI runner.
pid=$(SIMCTL_CHILD_MWIN_TEST_OUT="$out" xcrun simctl launch --terminate-running-process \
    --stderr="$err" "$device" "$id" | sed 's/.*: //')
echo "launched as $pid in $(($(date +%s) - start)) s"
launched=$(date +%s)
ran=0
while [ $(($(date +%s) - launched)) -lt 120 ] && ! grep -q '^result: ' "$out"; do
    commands=$(grep '^simctl: ' "$out" || true)
    count=$(printf '%s' "$commands" | grep -c '' || true)
    while [ "$ran" -lt "$count" ]; do
        ran=$((ran + 1))
        line=$(printf '%s\n' "$commands" | sed -n "${ran}p")
        echo "running: $line"
        # shellcheck disable=SC2086 # the verb and its arguments, as written
        set -- ${line#simctl: }
        verb=$1
        shift
        xcrun simctl "$verb" "$device" "$@" >/dev/null || echo "simctl $verb failed"
    done
    sleep 0.2
done
echo "waited until $(($(date +%s) - start)) s"
cat "$out"
status=1
if grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    cat "$err"
    # Where a stuck application is, or why it ended.
    if kill -0 "$pid" 2>/dev/null; then
        stacks=$(mktemp -t mwin-ios-stacks)
        sample "$pid" 1 -file "$stacks" >/dev/null 2>&1 &
        sleep 15
        grep -v '^ *$' "$stacks" | head -100 || true
        rm -f "$stacks"
    else
        report=$(ls -t "$HOME"/Library/Logs/DiagnosticReports/"$name"* 2>/dev/null | head -1)
        [ -n "$report" ] && head -c 6000 "$report"
    fi
fi
xcrun simctl terminate "$device" "$id" >/dev/null 2>&1 || true
rm -f "$out" "$err"
exit $status
