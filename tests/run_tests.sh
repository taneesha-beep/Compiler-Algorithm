#!/bin/sh
# Builds the project and diffs each tests/*.algo output against its
# matching tests/*.expected golden file.
set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" >/dev/null

BINARY="$BUILD_DIR/algo"
TESTS_DIR="$REPO_ROOT/tests"
ACTUAL_TMP="$(mktemp)"
trap 'rm -f "$ACTUAL_TMP"' EXIT

fail=0

for algo_file in "$TESTS_DIR"/*.algo; do
    name="$(basename "$algo_file" .algo)"
    expected="$TESTS_DIR/$name.expected"

    if [ ! -f "$expected" ]; then
        echo "SKIP $name (no .expected file)"
        continue
    fi

    "$BINARY" "$algo_file" > "$ACTUAL_TMP"

    if diff -q "$expected" "$ACTUAL_TMP" >/dev/null; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        diff "$expected" "$ACTUAL_TMP" || true
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "SOME TESTS FAILED"
    exit 1
fi

echo "ALL TESTS PASSED"
exit 0
