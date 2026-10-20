#!/usr/bin/env bash
# Run after preparing vcan0; each report remains beside its evidence.
set -euo pipefail
cd "$(dirname "$0")/.."
for version in 2015 2023; do
    python3 tests/run_scenarios.py --real-time --case "normal_${version}_charger_stop"
    cp build/scenarios/report.json "build/scenarios/report_real_time_${version}.json"
done
