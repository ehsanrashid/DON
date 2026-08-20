#!/bin/sh

# Usage: check_universal_macos.sh DON_EXE

set -eu

DON_EXE=$1

extract() {
    awk -F: -v lbl="$1" '$0 ~ lbl {
        sub(/^[[:space:]]+/, "", $2)
        sub(/[[:space:]]+$/, "", $2)
        print $2
        exit
    }'
}

BINARY_SIZE=$(wc -c < "$DON_EXE")
MAX_SIZE=$((150 * 1024 * 1024))

if [ "$BINARY_SIZE" -gt "$MAX_SIZE" ]; then
    printf 'check_universal_macos.sh: binary size %d bytes exceeds %d MB limit\n' \
            "$BINARY_SIZE" "$((MAX_SIZE / 1024 / 1024))" >&2
    exit 1
fi

FAIL=0

native_bench=$(arch -arm64 "$DON_EXE" bench 2>&1 | extract "Total nodes" || true)
if [ -z "$native_bench" ]; then
    echo "check_universal_macos.sh: arm64 run produced no bench" >&2
    FAIL=1
else
    printf 'native (arm64) bench %s\n' "$native_bench" >&2
fi

x86_bench=$(arch -x86_64 "$DON_EXE" bench 2>&1 | extract "Total nodes" || true)
if [ -z "$x86_bench" ]; then
    echo "check_universal_macos.sh: x86_64 (Rosetta) run produced no bench (crashed?)" >&2
    FAIL=1
else
    printf 'x86_64 (Rosetta) bench %s\n' "$x86_bench" >&2
fi

# Should match
if [ "$native_bench" != "$x86_bench" ]; then
    printf 'check_universal_macos.sh: slice bench mismatch: arm64=%s x86_64=%s\n' \
        "$native_bench" "$x86_bench" >&2
    FAIL=1
fi

# Check that under Rosetta, SSE4.1 is detected
x86_compiler=$(arch -x86_64 "$DON_EXE" compiler 2>&1 || true)
if printf '%s\n' "$x86_compiler" | grep -q 'SSE4.1'; then
    echo "x86_64 (Rosetta) compiler reports SSE4.1 ok" >&2
else
    echo "check_universal_macos.sh: x86_64 compiler output missing SSE4.1" >&2
    printf '%s\n' "$x86_compiler" >&2
    FAIL=1
fi

if [ "$FAIL" != 0 ]; then
    echo "check_universal_macos.sh: failed"
    exit 1
fi

echo "check_universal_macos.sh: Good! Universal macOS binary works."
