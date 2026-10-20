#!/bin/bash
# build-armhf.sh - cross-compile the simulator for ARM (hard-float).
# Usage:
#   ./scripts/build-armhf.sh           # incremental build
#   ./scripts/build-armhf.sh clean     # wipe the build dir and reconfigure
#   ./scripts/build-armhf.sh verify    # only check the artifact architecture

set -e
cd "$(dirname "$0")/.."

BUILD=build-armhf
TOOLCHAIN=cmake/toolchains/arm-linux-gnueabihf.cmake
EXPECTED_CC=arm-linux-gnueabihf-gcc
BIN=$BUILD/gbt27930_simulator

case "${1:-build}" in
    clean)
        echo "[setup] Removing $BUILD"
        rm -rf "$BUILD"
        ;;
    verify)
        if [ ! -f "$BIN" ]; then
            echo "[error] $BIN not built yet"; exit 1
        fi
        echo "=== file ==="
        file "$BIN"
        echo "=== ELF header ==="
        readelf -h "$BIN" | grep -E "Class|Data|Machine"
        echo "=== ARM ABI ==="
        readelf -A "$BIN" 2>/dev/null | grep -E "Tag_CPU_arch|Tag_FPU_arch|Tag_ABI_HardFP|Tag_ABI_VFP_args" || true
        if file "$BIN" | grep -q "ARM"; then
            echo "[ok] ARM binary confirmed"
        else
            echo "[FAIL] Not an ARM binary!"; exit 1
        fi
        exit 0
        ;;
esac

# Configure only when the build dir is missing or cached a different compiler.
NEEDS_CONFIG=true
if [ -f "$BUILD/CMakeCache.txt" ]; then
    CACHED_CC=$(grep "^CMAKE_C_COMPILER:[A-Z]*=" "$BUILD/CMakeCache.txt" | cut -d= -f2-)
    if [[ "$CACHED_CC" == *"$EXPECTED_CC"* ]]; then
        NEEDS_CONFIG=false
        echo "[skip] $BUILD already configured with $CACHED_CC"
    else
        echo "[setup] Cache has wrong compiler: $CACHED_CC != $EXPECTED_CC, reconfiguring"
        rm -rf "$BUILD"
    fi
fi

if [ "$NEEDS_CONFIG" = true ]; then
    echo "[setup] Configuring with toolchain $TOOLCHAIN"
    cmake -G Ninja -S . -B "$BUILD" -DPLATFORM=linux -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
fi

echo "[build] cmake --build $BUILD -j$(nproc)"
cmake --build "$BUILD" -j"$(nproc)"

if file "$BIN" | grep -q "ARM"; then
    echo "[ok] Built ARM binary: $BIN"
    file "$BIN"
else
    echo "[FAIL] Built binary is not ARM!"; file "$BIN"; exit 1
fi