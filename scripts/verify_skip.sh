#!/usr/bin/env bash
# Requires permission to create an isolated network namespace.
set -euo pipefail
cd "$(dirname "$0")/.."
set +e
unshare -n python3 tests/run_scenarios.py --build-dir build-skip
result=$?
set -e
if [ "$result" -ne 77 ]; then
    echo "FAIL: missing-vcan runner returned $result instead of 77"
    exit 1
fi
echo "PASS: unavailable vcan0 is reported as SKIP (77)"
