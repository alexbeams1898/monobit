#!/usr/bin/env bash
# test.sh - build and run engine-tests + selva-oscura-tests.
#
# Usage:
#   ./games/selva-oscura/scripts/test.sh          # build + run all tests
#   ./games/selva-oscura/scripts/test.sh -v       # verbose: show individual test case output

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
VERBOSE=""

for arg in "$@"; do
    case "$arg" in
        -v|--verbose) VERBOSE="--verbose" ;;
    esac
done

if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: Build directory not found: $BUILD_DIR" >&2
    echo "Run a CMake configure first (e.g. F7 in VS Code)." >&2
    exit 1
fi

echo "=== Building engine-tests ==="
cmake --build "$BUILD_DIR" --target engine-tests

echo ""
echo "=== Building selva-oscura-tests ==="
cmake --build "$BUILD_DIR" --target selva-oscura-tests

echo ""
echo "=== Running tests ==="
cd "$REPO_ROOT"
ctest --test-dir "$BUILD_DIR" --output-on-failure $VERBOSE
