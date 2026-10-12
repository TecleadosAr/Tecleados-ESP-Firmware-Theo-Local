#!/usr/bin/env bash
#
# TEF Firmware — Host Test Runner
#
# Compiles and runs all unit tests on the host machine.
# No ESP-IDF or target hardware required.
#
# Usage:
#   cd test/
#   bash run_tests.sh                 # normal build
#   bash run_tests.sh --asan          # AddressSanitizer + UBSan build
#   bash run_tests.sh --tsan          # ThreadSanitizer build
#   bash run_tests.sh --asan -- --junit   # args after "--" go to the test runner
#
# Requirements:
#   - GCC or Clang with gnu11 support (needed for <stdatomic.h> and the
#     C23-style enum syntax used by production headers).
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CC="${CC:-gcc}"
CFLAGS="-std=gnu11 -Wall -Wextra -Wpedantic -Wno-unused-function -Wno-unused-variable -O2"
OUTPUT="test_runner"
LDFLAGS="-lm"

# Mock/shim headers (test/include) come first so they shadow ESP-IDF headers,
# followed by the public include dir of every component.
INCLUDES="-Iinclude"
while IFS= read -r inc; do
    INCLUDES="$INCLUDES -I$inc"
done < <(find ../components -maxdepth 3 -type d -name include | sort)

SANITIZE=""
RUNNER_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --asan)
            SANITIZE="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
            CFLAGS="${CFLAGS/-O2/-O1 -g}"
            ;;
        --tsan)
            SANITIZE="-fsanitize=thread -g"
            ;;
        --)
            shift
            RUNNER_ARGS=("$@")
            break
            ;;
        *)
            RUNNER_ARGS+=("$1")
            ;;
    esac
    shift
done

echo "============================================"
echo "  TEF Firmware — Building Host Tests"
echo "============================================"
echo ""
echo "Compiler: $CC"
echo "Flags:    $CFLAGS $SANITIZE"
echo ""

# Compile (explicit status check: under `set -e` a failure would exit silently)
# shellcheck disable=SC2086
if ! $CC $CFLAGS $SANITIZE $INCLUDES -o "$OUTPUT" main.c $LDFLAGS 2>&1; then
    echo ""
    echo "BUILD FAILED"
    exit 1
fi

echo "Build successful."
echo ""

# Run
EXIT_CODE=0
./"$OUTPUT" ${RUNNER_ARGS[@]+"${RUNNER_ARGS[@]}"} || EXIT_CODE=$?

# Cleanup (optional — comment out to keep the binary)
# rm -f "$OUTPUT"

exit $EXIT_CODE
