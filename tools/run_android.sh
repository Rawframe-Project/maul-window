#!/bin/sh
# Runs a test executable in the Android emulator (mwin-0026), as CTest's
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
"$adb" shell "cd $dir && ./$name$quoted"
status=$?
"$adb" shell rm -f "$dir/$name" > /dev/null
exit $status
