#!/usr/bin/env bash
# Cross-platform compile gates and offline memory-safety regression.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 scripts/check_dependencies.py
bash scripts/build-armhf.sh
bash scripts/build-armhf.sh verify
cmake -G Ninja -DPLATFORM=zephyr -S . -B build-zephyr
cmake --build build-zephyr -j
cmake -G Ninja -DPLATFORM=linux -S . -B build-sanitize \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -no-pie"
cmake --build build-sanitize -j
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    ctest --test-dir build-sanitize --output-on-failure -R '^(message_lifecycle|can_capture|sim_log|sim_core|sim_fault|sim_engine|simulator_cli)$'
test -s build/compile_commands.json
test -s build-armhf/compile_commands.json
test -s build-zephyr/compile_commands.json
