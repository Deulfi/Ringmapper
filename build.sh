#!/bin/sh
# Build the ring_mapper binary with the Android NDK.
# Dynamic against bionic (always present on Android), stripped, size-optimized.
# API 26 = Android 8.0+; raise only if a newer libc API is ever needed.
#
# Usage: NDK=/path/to/android-ndk-r30 ./build.sh
#   (defaults to ./android-ndk-r30-windows/android-ndk-r30)
set -e
cd "$(dirname "$0")"
NDK="${NDK:-android-ndk-r30-windows/android-ndk-r30}"
API=26

BIN=$(ls -d "$NDK"/toolchains/llvm/prebuilt/*/bin | head -n1)
CC="$BIN/aarch64-linux-android${API}-clang"
[ -x "$CC" ] || CC="$CC.cmd"   # Windows NDK ships .cmd wrappers

"$CC" -Os -s -Wall -Wno-misleading-indentation -o ring_mapper ring_mapper.c
ls -l ring_mapper
