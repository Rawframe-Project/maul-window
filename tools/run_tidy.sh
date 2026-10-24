#!/usr/bin/env bash
# Runs clang-tidy over the library's own sources in a build tree's
# compile_commands.json, JOBS files at a time (default: the processors):
#
#   tools/run_tidy.sh <build directory>
#
# Each file's output goes to <build directory>/tidy/. Once every file has
# run, each prints one line in order, "clean" or "FAIL", and a failing
# file's diagnostics follow its line. Exits 1 when any file failed, or
# when the build lists none of the library's sources; on GitHub Actions
# each failure is also an error on its file.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "$1" && pwd)
jobs=${JOBS:-$(nproc)}
logs=$build/tidy
rm -rf "$logs"
mkdir -p "$logs"
python3 - "$build/compile_commands.json" >"$logs/files.txt" <<'EOF'
import json, sys
files = {e["file"] for e in json.load(open(sys.argv[1]))}
print("\n".join(sorted(f for f in files if "/src/" in f and "_deps" not in f and "/generated/" not in f)))
EOF
# A run that checks nothing must not pass.
if ! grep -q . "$logs/files.txt"; then
    echo "FAIL no library sources in $build/compile_commands.json"
    exit 1
fi

# One file: its output in its log, and a marker beside it when it failed.
tidy_one() {
    local log
    log=$2/$(basename "$1").log
    clang-tidy-20 -p "$3" --quiet --warnings-as-errors='*' "$1" >"$log" 2>&1 || : >"$log.failed"
}
export -f tidy_one
xargs -P "$jobs" -I {} bash -c 'tidy_one "$1" "$2" "$3"' _ {} "$logs" "$build" <"$logs/files.txt"

failed=0
while read -r file; do
    name=${file#"$root/"}
    log=$logs/$(basename "$file").log
    if [ -e "$log.failed" ]; then
        echo "FAIL $name"
        # Without the count of the warnings clang-tidy suppressed.
        grep -v -E '^[0-9]+ warnings? (and [0-9]+ errors? )?generated\.$' "$log" || true
        if [ -n "${GITHUB_ACTIONS:-}" ]; then
            echo "::error file=$name::clang-tidy found problems in $name"
        fi
        failed=1
    else
        echo "clean $name"
    fi
done <"$logs/files.txt"
exit "$failed"
