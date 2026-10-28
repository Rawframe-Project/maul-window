#!/bin/sh
# Runs a test executable in the Android emulator, as CTest's
# cross-compiling emulator (cmake/android-emulator.cmake):
#
#   tools/run_android.sh <executable> [arguments...]
#
# Pushes it to /data/local/tmp/maul-window/ on the device ANDROID_SERIAL
# names (else the only one), runs it there and exits with its status.
set -eu
adb=${ADB:-adb}
exe=$1
shift
dir=/data/local/tmp/maul-window
name=$(basename "$exe")
"$adb" shell mkdir -p "$dir" > /dev/null
"$adb" push "$exe" "$dir/$name" > /dev/null 2>&1
quoted=""
for arg in "$@"; do
    quoted="$quoted '$(printf '%s' "$arg" | sed "s/'/'\\\\''/g")'"
done
# adb's shell protocol carries the remote exit status.
set +e
if [ -n "${MWIN_COVERAGE_DIR:-}" ]; then
    # Probe only: the profile is written at exit and pulled to the host.
    "$adb" shell "mkdir -p $dir/prof && cd $dir && LLVM_PROFILE_FILE=$dir/prof/%p-%m.profraw ./$name$quoted"
    status=$?
    for file in $("$adb" shell ls "$dir/prof" | tr -d '\r'); do
        "$adb" pull "$dir/prof/$file" "$MWIN_COVERAGE_DIR/$name-$file" > /dev/null
    done
    "$adb" shell rm -rf "$dir/prof" > /dev/null
else
    "$adb" shell "cd $dir && ./$name$quoted"
    status=$?
fi
"$adb" shell rm -f "$dir/$name" > /dev/null
exit $status
