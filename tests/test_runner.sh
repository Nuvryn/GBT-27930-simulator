#!/usr/bin/env bash
# Protocol scenarios use separate processes on an actual vcan socket.
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 tests/run_scenarios.py "$@"
