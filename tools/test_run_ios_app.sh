#!/bin/sh
# Drives tools/run_ios_app.sh on any host with a POSIX shell and python3,
# against stand-ins for simctl, idb, PlistBuddy and a test application,
# with date and sleep running 50 times as fast. The stand-in simulator
# drops the first N launches that bring the application back: 0, 1 or 2
# pass with that many sent again, 3 fail with the runner's line saying
# so, and an application that never ends is ended by the runner's limit.
# An application that asks for a touch gets it from idb, given the
# booted device's UDID. A launch FrontBoard refuses once is installed and
# launched again; refused twice, or refused for another reason, or
# answered without a pid, the run fails at once.
# Run by tools/check.sh and CI's checks job, in seconds, so that a slip
# in the runner fails here rather than after a simulator's boot.
#
#   tools/test_run_ios_app.sh
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/mwin-ios-runner.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/bin" "$work/app.app"

# The runner reads the bundle identifier through PlistBuddy's own path.
sed "s#/usr/libexec/PlistBuddy#$work/bin/PlistBuddy#" "$root/tools/run_ios_app.sh" >"$work/run.sh"
printf '#!/bin/sh\necho org.test.app\n' >"$work/bin/PlistBuddy"

# A clock 50 times as fast, from when this script began.
cat >"$work/bin/date" <<'EOF'
#!/bin/sh
[ "$*" = +%s ] || exec /bin/date "$@"
exec python3 -c 'import os, time
t0 = float(os.environ["FAKE_T0"])
print(int(t0 + (time.time() - t0) * 50))'
EOF
cat >"$work/bin/sleep" <<'EOF'
#!/bin/sh
exec python3 -c 'import sys, time; time.sleep(float(sys.argv[1]) / 50)' "$1"
EOF

# simctl: the first launch of the application starts it, unless
# FAKE_REFUSE says to refuse it (a count of FrontBoard refusals, "other"
# for another error, "nopid" for a launch that names no process); a
# later one is a launch back, which the application counts; terminate
# ends it. Each install is counted.
cat >"$work/bin/xcrun" <<'EOF'
#!/bin/sh
state=$FAKE_STATE
shift
verb=$1
shift
for last; do :; done
case "$verb $last" in
"list booted") echo "    iPhone 16 (0A1B2C3D-0000-4000-8000-00000000ABCD) (Booted)" ;;
"install "*) echo install >>"$state/installs" ;;
"launch com.apple.Preferences") touch "$state/settings" ;;
"launch org.test.app")
    if [ -f "$state/pid" ]; then
        echo back >>"$state/backs"
    elif [ "$FAKE_REFUSE" = other ]; then
        echo "An error was encountered processing the command (domain=NSPOSIXErrorDomain, code=3):" >&2
        exit 3
    elif [ "$FAKE_REFUSE" = nopid ]; then
        exit 0
    elif [ "$(wc -l <"$state/refused")" -lt "$FAKE_REFUSE" ]; then
        echo refused >>"$state/refused"
        echo "The request was denied by service delegate (SBMainWorkspace) for reason: NotFound" \
            "(\"Application \"org.test.app\" is unknown to FrontBoard\")." >&2
        exit 1
    else
        MWIN_TEST_OUT=$SIMCTL_CHILD_MWIN_TEST_OUT sh "$FAKE_APP" </dev/null >/dev/null 2>&1 &
        echo $! >"$state/pid"
        echo "org.test.app: $!"
    fi
    ;;
"terminate org.test.app") kill "$(cat "$state/pid")" 2>/dev/null || true ;;
esac
EOF

# idb: what it was asked, for the application to read.
cat >"$work/bin/idb" <<'EOF'
#!/bin/sh
echo "$*" >"$FAKE_STATE/idb"
EOF

# The window test's drive: active, Settings in front, brought back; it
# becomes active again only once the simulator has dropped FAKE_DROPS
# launches back, and ends 30 s later, past the runner's 20. With
# FAKE_DROPS "never" it asks for nothing and never ends.
cat >"$work/app.sh" <<'EOF'
out=$MWIN_TEST_OUT
echo "active at 0.10 s" >>"$out"
[ "$FAKE_DROPS" = never ] && exec sleep 100000
if [ "$FAKE_DROPS" = touch ]; then
    echo "idb: ui tap 10 20" >>"$out"
    until [ -f "$FAKE_STATE/idb" ]; do sleep 1; done
    failures=1
    [ "$(cat "$FAKE_STATE/idb")" = "ui tap 10 20 --udid 0A1B2C3D-0000-4000-8000-00000000ABCD" ] &&
        failures=0
    echo "result: $failures failures" >>"$out"
    exit
fi
echo "simctl: launch com.apple.Preferences" >>"$out"
until [ -f "$FAKE_STATE/settings" ]; do sleep 1; done
echo "simctl: launch org.test.app" >>"$out"
until [ "$(cat "$FAKE_STATE/backs" 2>/dev/null | wc -l)" -gt "$FAKE_DROPS" ]; do sleep 1; done
echo "active at 9.00 s" >>"$out"
sleep 30
echo "result: 0 failures" >>"$out"
EOF

chmod +x "$work/bin/"*
failures=0
# How the stand-in simulator answers the application's first launch.
refuse=0
# Drops, then the exit status and patterns of lines the runner must
# print (extended regular expressions, whole lines), "installs: N"
# among them for the installs it made.
check() {
    drops=$1
    want_status=$2
    shift 2
    rm -rf "$work/state"
    mkdir "$work/state"
    touch "$work/state/installs" "$work/state/refused"
    status=0
    PATH="$work/bin:$PATH" FAKE_T0=$(python3 -c 'import time; print(time.time())') \
        FAKE_STATE="$work/state" FAKE_APP="$work/app.sh" FAKE_DROPS=$drops FAKE_REFUSE=$refuse \
        sh "$work/run.sh" "$work/app.app" >"$work/log" 2>&1 &
    runner=$!
    # A runner that never ends fails here within a minute, and the
    # stand-in application never outlives its check.
    tenths=0
    while kill -0 "$runner" 2>/dev/null; do
        if [ "$tenths" -ge 600 ]; then
            kill "$runner"
            echo "the runner did not end within a minute" >>"$work/log"
            break
        fi
        sleep 0.1
        tenths=$((tenths + 1))
    done
    wait "$runner" || status=$?
    if [ -f "$work/state/pid" ]; then
        kill "$(cat "$work/state/pid")" 2>/dev/null || true
    fi
    echo "installs: $(wc -l <"$work/state/installs" | tr -d ' ')" >>"$work/log"
    ok=true
    [ "$status" -eq "$want_status" ] || ok=false
    for line; do
        grep -qxE "$line" "$work/log" || ok=false
    done
    if $ok; then
        echo "passed: drops $drops, refuse $refuse"
    else
        echo "FAILED: drops $drops, refuse $refuse: exit $status, want $want_status and:"
        printf '  %s\n' "$@"
        sed 's/^/  | /' "$work/log"
        failures=$((failures + 1))
    fi
}
check 0 0 "running: simctl: launch org.test.app" "waited until [0-9]+ s, the launch back sent again: 0" \
    "result: 0 failures"
check 1 0 "waited until [0-9]+ s, the launch back sent again: 1" "result: 0 failures"
check 2 0 "waited until [0-9]+ s, the launch back sent again: 2" "result: 0 failures"
check 3 1 "waited until [0-9]+ s, the launch back sent again: 2" \
    "still in the background 20 s after the launch back was sent again twice"
check never 1 "ended by the runner's 180 s limit, before the test's closing line"
check touch 0 "running: idb: ui tap 10 20" "result: 0 failures"
refuse=1
check 0 0 "the launch was refused, the application unknown to FrontBoard: installing it again" \
    "launched as [0-9]+ in [0-9]+ s" "result: 0 failures" "installs: 2"
refuse=2
check 0 1 ".*is unknown to FrontBoard.*" "the launch failed after [0-9]+ s, before the test ran" \
    "installs: 2"
refuse=other
check 0 1 "An error was encountered .*code=3\):" \
    "the launch failed after [0-9]+ s, before the test ran" "installs: 1"
refuse=nopid
check 0 1 "the launch failed after [0-9]+ s, before the test ran" "installs: 1"
[ "$failures" -eq 0 ]
