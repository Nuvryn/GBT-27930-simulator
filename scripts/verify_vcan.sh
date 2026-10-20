#!/usr/bin/env bash
# Prepare vcan in the current network namespace and run the CAN integration check.
set -euo pipefail
cd "$(dirname "$0")/.."
if ! ip link show vcan0 >/dev/null 2>&1; then
    ip link add dev vcan0 type vcan
fi
ip link set vcan0 up
./build/test_can_hal
