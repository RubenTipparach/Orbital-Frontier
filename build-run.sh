#!/usr/bin/env bash
set -euo pipefail

echo "=== Orbital Frontier: Native Build ==="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Compile shaders if sokol-shdc is available
SHDC="$SCRIPT_DIR/tools/sokol-shdc"
if [ -f "$SHDC.exe" ]; then
    SHDC="$SHDC.exe"
fi
if [ -x "$SHDC" ]; then
    SHADER_SLANG="hlsl5:glsl430:glsl300es:metal_macos:wgsl"
    NEEDS_COMPILE=false
    for f in shaders/*.glsl; do
        if [ "$f" -nt "${f}.h" ]; then
            NEEDS_COMPILE=true
            break
        fi
    done
    if $NEEDS_COMPILE; then
        echo "[0/3] Recompiling shaders..."
        for f in shaders/*.glsl; do
            "$SHDC" --input "$f" --output "${f}.h" --slang "$SHADER_SLANG"
        done
    fi
fi

# Configure if needed
if [ ! -f build/CMakeCache.txt ]; then
    echo "[1/3] Configuring CMake..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
else
    echo "[1/3] CMake already configured"
fi

# Build
echo "[2/3] Building (Release)..."
cmake --build build --config Release

# Run
echo "[3/3] Launching orbital-frontier..."
if [ -f build/orbital-frontier ]; then
    ./build/orbital-frontier
elif [ -f build/Release/orbital-frontier ]; then
    ./build/Release/orbital-frontier
elif [ -f build/orbital-frontier.exe ]; then
    ./build/orbital-frontier.exe
elif [ -f build/Release/orbital-frontier.exe ]; then
    ./build/Release/orbital-frontier.exe
else
    echo "ERROR: orbital-frontier binary not found in build/"
    exit 1
fi
