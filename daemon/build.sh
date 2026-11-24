#!/bin/sh
# Build the ring_mapper daemon with the Android NDK -> ../module/ring_mapper
# (the app bundles that same file, so build this before building the app).
#
# Dynamic against bionic (always present on Android), stripped, size-optimized.
# API 26 = Android 8.0+; raise only if a newer libc API is ever needed.
#
# Usage: NDK=/path/to/android-ndk ./build.sh
#   (default: ../android-ndk-r30-windows/android-ndk-r30)
set -e
cd "$(dirname "$0")"
NDK="${NDK:-../android-ndk-r30-windows/android-ndk-r30}"
API=26
OUT=../module/ring_mapper

BIN=$(ls -d "$NDK"/toolchains/llvm/prebuilt/*/bin | head -n1)
CC="$BIN/aarch64-linux-android${API}-clang"
[ -x "$CC" ] || CC="$CC.cmd"   # the Windows NDK ships .cmd wrappers

"$CC" -Os -s -Wall -Wno-misleading-indentation -o "$OUT" ring_mapper.c
ls -l "$OUT"
