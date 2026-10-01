#!/bin/sh
# Builds a test application for the Android emulator (mwin-0026), with
# the SDK's tools and no Gradle:
#
#   tools/build_android_app.sh <out.apk> <libname.so> <package> [abi]
#
# The library's Java activity (java/maul/window) and the tests' Java
# (test/android/java) compiled with javac and d8,
# test/android/AndroidManifest.xml with the package and library filled
# in, linked by aapt2 as debuggable with the tests' resources
# (test/android/res), the native library under
# lib/<abi>/ (x86_64 by default), aligned and signed with a debug key
# kept beside the output. ANDROID_HOME names the SDK; the newest build
# tools and platform there are used.
set -eu
out=$1
library=$2
package=$3
abi=${4:-x86_64}
root=$(cd "$(dirname "$0")/.." && pwd)
tools=$(ls -d "$ANDROID_HOME"/build-tools/* | sort -V | tail -1)
jar=$(ls -d "$ANDROID_HOME"/platforms/android-* | sort -V | tail -1)/android.jar
name=$(basename "$library" .so)
name=${name#lib}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
sed -e "s/@PACKAGE@/$package/" -e "s/@LIBRARY@/$name/" \
    "$root/test/android/AndroidManifest.xml" > "$work/AndroidManifest.xml"
mkdir -p "$work/classes" "$work/dex" "$work/lib/$abi"
javac -nowarn --release 11 -classpath "$jar" -d "$work/classes" \
    $(find "$root/java" "$root/test/android/java" -name '*.java')
"$tools/d8" --min-api 30 --lib "$jar" --output "$work/dex" \
    $(find "$work/classes" -name '*.class')
"$tools/aapt2" compile --dir "$root/test/android/res" -o "$work/res.zip"
"$tools/aapt2" link -o "$work/linked.apk" -I "$jar" --manifest "$work/AndroidManifest.xml" \
    "$work/res.zip" \
    --min-sdk-version 30 --target-sdk-version 35 --debug-mode
cp "$library" "$work/lib/$abi/"
cp "$work/dex/classes.dex" "$work/"
(cd "$work" && zip -q linked.apk classes.dex "lib/$abi/$(basename "$library")")
"$tools/zipalign" -f -p 4 "$work/linked.apk" "$work/aligned.apk"
key=$(dirname "$out")/debug.keystore
if [ ! -f "$key" ]; then
    keytool -genkeypair -keystore "$key" -storepass android -keypass android \
        -alias debug -keyalg RSA -validity 10000 -dname "CN=Maul Window test" > /dev/null 2>&1
fi
"$tools/apksigner" sign --ks "$key" --ks-pass pass:android --out "$out" "$work/aligned.apk"
