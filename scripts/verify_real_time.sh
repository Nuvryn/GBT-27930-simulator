#!/usr/bin/env bash
# Run after preparing vcan0; each report remains beside its evidence.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "${1:-}" = "--prepare-vcan" ]; then
    if ! ip link show vcan0 >/dev/null 2>&1; then
        ip link add dev vcan0 type vcan
    fi
    ip link set vcan0 up
fi
for version in 2015 2023; do
    python3 tests/run_scenarios.py --real-time --case "normal_${version}_charger_stop"
    cp build/scenarios/report.json "build/scenarios/report_real_time_${version}.json"
done
