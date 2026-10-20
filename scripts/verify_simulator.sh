#!/usr/bin/env bash
# Full local regression. Run as a user with an already prepared vcan0.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -G Ninja -DPLATFORM=linux -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B build
cmake --build build -j
if [ "${1:-}" = "--prepare-vcan" ]; then
    if ! ip link show vcan0 >/dev/null 2>&1; then
        ip link add dev vcan0 type vcan
    fi
    ip link set vcan0 up
    shift
fi
if [ "$#" -gt 0 ]; then
    ctest --test-dir build --output-on-failure -E simulator_scenarios
    bash tests/test_runner.sh "$@"
else
    ctest --test-dir build --output-on-failure
fi
