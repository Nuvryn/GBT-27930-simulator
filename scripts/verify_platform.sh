#!/usr/bin/env bash
# Build and check the imported platform layer from the repository root.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -G Ninja -DPLATFORM=linux -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B build
cmake --build build -j
test -s build/compile_commands.json
ctest --test-dir build --output-on-failure

# WILL_FAIL cannot distinguish an ordinary error from a signal: check it here too.
set +e
./build/scaffold_demo --config config/missing.json >build/config_missing.log 2>&1
config_rc=$?
set -e
if [ "$config_rc" -ne 1 ]; then
    echo "FAIL: missing config returned $config_rc (expected 1)"
    exit 1
fi
grep -q 'config_source_load failed' build/config_missing.log

# A missing interface is explicitly a skip, never a passing CAN exchange.
set +e
./build/test_can_hal
can_rc=$?
set -e
case "$can_rc" in
    0) echo 'PASS: CAN integration' ;;
    77) echo 'SKIP: CAN integration requires vcan0' ;;
    *) echo "FAIL: CAN integration returned $can_rc"; exit "$can_rc" ;;
esac
