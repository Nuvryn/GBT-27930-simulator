#!/usr/bin/env bash
# Optional UI gates; memory display tests need no physical display or new libraries.
set -euo pipefail
cd "$(dirname "$0")/.."
prepare=false
arm=false
sanitize=false
for argument in "$@"; do
    case "$argument" in
        --prepare-vcan) prepare=true ;;
        --arm) arm=true ;;
        --sanitize) sanitize=true ;;
        *) echo "Usage: $0 [--prepare-vcan] [--arm] [--sanitize]" >&2; exit 2 ;;
    esac
done
if $prepare; then
    if ! ip link show vcan0 >/dev/null 2>&1; then ip link add dev vcan0 type vcan; fi
    ip link set vcan0 up
fi
cmake -G Ninja -DPLATFORM=linux -DSIM_UI=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B build-ui
cmake --build build-ui -j
ctest --test-dir build-ui --output-on-failure
if $arm; then
    cmake -G Ninja -DPLATFORM=linux -DSIM_UI=ON -DBUILD_TESTING=OFF \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-linux-gnueabihf.cmake -S . -B build-armhf-ui
    cmake --build build-armhf-ui -j
    file build-armhf-ui/gbt27930_simulator
    test -s build-armhf-ui/compile_commands.json
fi
if $sanitize; then
    cmake -G Ninja -DPLATFORM=linux -DSIM_UI=ON -S . -B build-ui-sanitize \
        -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -no-pie"
    cmake --build build-ui-sanitize -j
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        ctest --test-dir build-ui-sanitize --output-on-failure -R '^(sim_ui|sim_control|control_concurrency|ui_startup|ui_vcan)$'
fi
