#!/bin/bash
# Glipt automated test runner

set -e

GLIPT="./glipt"
PASS=0
FAIL=0
FAILED_TESTS=""

run_test() {
    local file="$1"
    local name
    name=$(basename "$file")
    printf "  %-30s " "$name"
    if output=$($GLIPT run --allow-all "$file" 2>&1); then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        FAIL=$((FAIL + 1))
        FAILED_TESTS="$FAILED_TESTS  $file\n"
        echo "    Output: $(echo "$output" | tail -3)"
    fi
}

echo "Running Glipt tests..."
echo ""

# Core language tests
echo "Core:"
run_test examples/milestone1.glipt
run_test examples/milestone2.glipt
run_test examples/milestone3.glipt
run_test examples/full_test.glipt

# Feature tests
echo ""
echo "Features:"
run_test examples/exec_test.glipt
run_test examples/parallel_test.glipt

# Standard library tests
echo ""
echo "Stdlib:"
run_test examples/stdlib_test.glipt

# Phase 2 tests
echo ""
echo "Phase 2:"
run_test examples/math_test.glipt
run_test examples/regex_test.glipt
run_test examples/match_test.glipt
run_test examples/import_test.glipt

# Summary
echo ""
echo "---"
TOTAL=$((PASS + FAIL))
echo "$PASS/$TOTAL tests passed"

if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo "Failed tests:"
    printf "$FAILED_TESTS"
    exit 1
fi
