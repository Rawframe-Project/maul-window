#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The checks, in three tiers. Each tier includes the one before it.
#
#   tools/check.sh changed   the files changed against origin/main: the
#                            source rules, formatting, the documentation
#                            checks, and in the linux-clang-20-asan preset
#                            the tests the changes reach, integration tests
#                            left out. Under two minutes; run it before every
#                            push.
#   tools/check.sh pr        what the pull request check runs on Linux:
#                            every rule, the generated tables, the
#                            documentation site, clang-tidy, the whole
#                            linux-clang-20-asan suite and a short fuzzing
#                            pass.
#   tools/check.sh full      also the other Linux presets (ThreadSanitizer,
#                            shared, minimal, Clang and GCC Release), the
#                            virtual gamepad tests where /dev/uinput can be
#                            written, and longer fuzzing.
#
# The builds are the CI cells' CMake presets (CMakePresets.json), in
# out/<preset>; the other trees go to out/check-*. Needs what the
# organization's CI image has: gcc-14, clang-20, clang-tidy-20, ninja, the
# pinned clang-format, Xvfb, weston, dbus-daemon and the X11 and Wayland
# development files. The organization's documentation check runs when its
# .github checkout is at ORG_DEFAULTS (default: ../../.github from the
# repository). The platform cells (macOS, iOS, Android, Windows, the web)
# run in CI; docs/operations/checks.md says how to run each by hand.
set -euo pipefail

tier=${1:-changed}
case "$tier" in
changed | pr | full) ;;
*)
    echo "usage: tools/check.sh [changed|pr|full]" >&2
    exit 2
    ;;
esac

cd "$(dirname "$0")/.."
root=$PWD
out=$root/out
mkdir -p "$out"
org=${ORG_DEFAULTS:-$root/../../.github}
jobs=${JOBS:-8}

step() { printf '\n== %s\n' "$*"; }

# A tree outside the presets (clang-tidy, fuzzing).
configure() { # name, then cmake arguments
    local name=check-$1
    shift
    cmake -S "$root" -B "$out/$name" -G Ninja -DMAUL_WINDOW_WERROR=ON "$@" >"$out/$name.log"
}

# A CI cell: its configure, build and test presets. More ctest arguments
# (a label filter) follow the preset's name.
preset() {
    local name=$1
    shift
    step "preset $name"
    cmake --preset "$name" >"$out/$name.log"
    cmake --build --preset "$name" -j "$jobs" >>"$out/$name.log"
    ctest --preset "$name" -j "$jobs" "$@"
}

# The ctest labels a changed path reaches, as a regular expression; "all"
# for every test (shared code, the build, the headers), nothing for a path
# no Linux test runs.
labels_for() {
    local path=$1 name
    name=$(basename "$path")
    name=${name%.*}
    name=${name#test_}
    name=${name#backend_}
    case "$path" in
    *.md | LICENSE | .github/* | docs/* | samples/* | bench/* | java/* | test/android/* | test/ios/*) ;;
    tools/check_*.py | tools/build_docs.py | tools/gen_api.py | tools/run_* | tools/build_android_app.sh) ;;
    src/generated/pad_* | tools/gamepaddb/* | tools/gen_gamepad_db.py) echo "^(gamepad|pad_map|linux_gamepad)$" ;;
    src/generated/android_keys.c | tools/androidkeys/* | tools/gen_android_keys.py) ;;
    test/fuzz_*) ;;
    src/win32_* | src/macos_* | src/ios_* | src/apple_* | src/web_* | test/test_win32_* | \
        test/test_macos_* | test/test_ios_* | test/test_web_* | test/web_* | tools/gen_web_glue.py) ;;
    src/android_motion.* | src/android_name.* | src/android_pad_map.*) echo "^$name$" ;;
    src/android* | test/test_android*) ;;
    src/x11_* | src/backend_x11.c | test/test_x11* | test/x11_*) echo "^x11" ;;
    src/wayland_* | src/backend_wayland.c | test/test_wayland* | test/wayland_*) echo "^wayland" ;;
    src/linux_* | src/dbus_api.* | test/test_linux_* | test/linux_*) echo "^(linux_|x11_ime$)" ;;
    src/*.[ch] | test/test_*.c)
        if [ -f "test/test_$name.c" ]; then echo "^$name$"; else echo all; fi
        ;;
    *) echo all ;;
    esac
}

source_checks() {
    step "source, module, length, documentation and snippet rules"
    python3 tools/check_source.py
    python3 tools/check_modules.py
    python3 tools/check_lengths.py
    python3 tools/check_docs.py
    python3 tools/check_guide.py
    if [ -f "$org/scripts/docs_check.py" ]; then
        python3 "$org/scripts/docs_check.py" --strict --exclude protocols --exclude tools/gamepaddb \
            --exclude tools/androidkeys
    else
        echo "skipped the organization's documentation check: no checkout at $org"
    fi
}

# Every C file of the project but those kept as published.
all_c_files() {
    git ls-files 'include/*.h' 'src/*.[chm]' 'test/*.[chm]' 'tools/*.[ch]' 'bench/*.[ch]' \
        'samples/*.[ch]' | grep -v -E '^(tools/gamepaddb|tools/androidkeys)/'
}

if [ "$tier" = changed ]; then
    base=$(git merge-base HEAD origin/main 2>/dev/null || echo HEAD)
    mapfile -t changed < <({
        git diff --name-only "$base"
        git ls-files --others --exclude-standard
    } | sort -u)
    if [ ${#changed[@]} -eq 0 ]; then
        echo "nothing changed against origin/main"
        exit 0
    fi
    source_checks
    mapfile -t c_files < <(printf '%s\n' "${changed[@]}" | grep -E '\.[chm]$' |
        grep -v -E '^(src/generated|tools/gamepaddb|tools/androidkeys)/' || true)
    present=()
    for f in "${c_files[@]+"${c_files[@]}"}"; do [ -f "$f" ] && present+=("$f"); done
    if [ ${#present[@]} -gt 0 ]; then
        step "clang-format on ${#present[@]} changed files"
        clang-format --dry-run -Werror "${present[@]}"
    fi
    labels=""
    for f in "${changed[@]}"; do labels="$labels $(labels_for "$f")"; done
    if [ -z "${labels// /}" ]; then
        echo "no Linux test reached by the changes"
        exit 0
    fi
    step "preset linux-clang-20-asan, integration tests left out"
    cmake --preset linux-clang-20-asan >"$out/linux-clang-20-asan.log"
    cmake --build --preset linux-clang-20-asan -j "$jobs" >>"$out/linux-clang-20-asan.log"
    # By the build tree rather than the test preset, whose own label filter
    # would replace -LE.
    tests=(ctest --test-dir "$out/linux-clang-20-asan" --output-on-failure --timeout 300 -j "$jobs"
        -LE '^(integration|uinput)$')
    if [[ " $labels " != *" all "* ]]; then
        regex=$(printf '%s\n' $labels | sort -u | paste -sd '|')
        tests+=(-L "$regex")
        echo "tests labeled $regex"
    fi
    reached=$("${tests[@]}" -N | grep -c 'Test *#' || true)
    if [ "$reached" -gt 0 ]; then
        "${tests[@]}"
    else
        echo "no test outside the integration tests is reached"
    fi
    exit 0
fi

source_checks

step "clang-format on every file"
clang-format --dry-run -Werror $(all_c_files)

step "generated tables match their generators"
mkdir -p "$out/regenerated"
python3 tools/gen_gamepad_db.py Linux "$out/regenerated/pad_linux.c"
python3 tools/gen_gamepad_db.py Windows "$out/regenerated/pad_windows.c"
python3 tools/gen_android_keys.py "$out/regenerated/android_keys.c"
for f in pad_linux.c pad_windows.c android_keys.c; do
    diff -u "src/generated/$f" "$out/regenerated/$f"
done

step "the documentation site builds"
python3 tools/build_docs.py >"$out/site.log"

step "clang-tidy on the Linux build's sources"
configure tidy -DCMAKE_C_COMPILER=clang-20 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAUL_WINDOW_BUILD_TESTS=OFF
cmake --build "$out/check-tidy" -j "$jobs" >>"$out/check-tidy.log"
JOBS=$jobs tools/run_tidy.sh "$out/check-tidy"

preset linux-clang-20-asan

fuzz_seconds=10
[ "$tier" = full ] && fuzz_seconds=60
step "fuzzing, $fuzz_seconds seconds per target"
configure fuzz -DCMAKE_C_COMPILER=clang-20 -DMAUL_WINDOW_FUZZ=ON -DMAUL_WINDOW_TEST_BACKEND=ON \
    -DMAUL_WINDOW_BUILD_TESTS=OFF
cmake --build "$out/check-fuzz" -j "$jobs" >>"$out/check-fuzz.log"
for target in "$out"/check-fuzz/fuzz_*; do
    name=$(basename "$target")
    corpus=$out/corpus/$name
    mkdir -p "$corpus"
    # The committed seeds, read; what the run finds goes to the first.
    seeds=()
    [ -d "test/seeds/$name" ] && seeds=("test/seeds/$name")
    "$target" -max_total_time=$fuzz_seconds -max_len=256 "$corpus" "${seeds[@]}" >"$out/$name.log" 2>&1 ||
        {
            tail -30 "$out/$name.log"
            exit 1
        }
    echo "$name: no finding"
done

[ "$tier" = full ] || exit 0

for name in linux-clang-20-tsan linux-shared linux-minimal linux-clang-20 linux-gcc-14; do
    preset "$name"
done

if [ -w /dev/uinput ]; then
    step "virtual gamepads through /dev/uinput"
    ctest --test-dir "$out/linux-clang-20-asan" -L '^uinput$' --output-on-failure
else
    echo "skipped the virtual gamepad tests: /dev/uinput cannot be written"
fi

step "all checks passed"
