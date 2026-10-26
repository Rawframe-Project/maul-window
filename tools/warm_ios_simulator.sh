#!/bin/sh
# Pays a simulator's first install and launch of a test application
# before the iOS tests that time theirs, as their setup fixture: the
# application given, installed and launched once. On a device never
# used before they take minutes; once done, seconds. Settings, which the
# window test brings in front of itself, is launched once beforehand
# (CI does it after the boot): its first launch alone can take longer
# than this fixture's timeout.
#
#   tools/warm_ios_simulator.sh APP
set -eu
app=$1
device=${MWIN_IOS_DEVICE:-booted}
id=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$app/Info.plist")
start=$(date +%s)
xcrun simctl install "$device" "$app"
echo "warm-up: installed $id in $(($(date +%s) - start)) s"
start=$(date +%s)
xcrun simctl launch "$device" "$id" >/dev/null
xcrun simctl terminate "$device" "$id"
echo "warm-up: launched $id in $(($(date +%s) - start)) s"
