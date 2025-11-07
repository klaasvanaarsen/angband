#!/usr/bin/env bash
set -euo pipefail

# Usage: ./build.sh [old|new|both]
TARGET=${1:-old}

SRC_DIR=$(pwd)
OLD_CMAKE="$SRC_DIR/CMakeLists.txt"
NEW_CMAKE="$SRC_DIR/new/CMakeLists.txt"

TOOLCHAIN_FILE="../toolchains/linux-i686-mingw32-cross.cmake"
EXTRA_CMAKE_OPTS="-G Ninja -DSUPPORT_WINDOWS_FRONTEND=ON -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE"

swap_cmake() {
    local which=$1
    if [ "$which" == "new" ]; then
        echo "Swapping in NEW CMakeLists.txt"
        cp "$NEW_CMAKE" "$OLD_CMAKE"
    elif [ "$which" == "old" ]; then
        echo "Restoring OLD CMakeLists.txt"
        git checkout "$OLD_CMAKE"
    fi
}

build() {
    local label=$1
    local build_dir="$SRC_DIR/build_$label"

    echo "=== Building $label in $build_dir ==="

    mkdir -p "$build_dir"
    cd "$build_dir"

    # Clean only this build folder
    rm -rf CMakeCache.txt CMakeFiles/

    # Run CMake & build
    cmake "$SRC_DIR" $EXTRA_CMAKE_OPTS
    cmake --build . -j"$(nproc)"
    cmake --install .

    echo "=== Finished building $label ==="
    cd "$SRC_DIR"
}

case "$TARGET" in
    old)
        swap_cmake old
        build "old"
        ;;
    new)
        swap_cmake new
        build "new"
        ;;
    both)
        swap_cmake old
        build "old"
        swap_cmake new
        build "new"
        ;;
    *)
        echo "Usage: $0 [old|new|both]"
        exit 1
        ;;
esac
