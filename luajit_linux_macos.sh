#!/bin/bash

# Build LuaJIT for Linux or macOS (produces universal static lib on macOS).
# Safe to run multiple times; always re-build cleanly for each architecture.

set -euo pipefail

# Resolve DIR (allow user override). Prefer script directory so you can invoke from anywhere.
: "${DIR:=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"

LUAJIT_SRC="$DIR/modules/LuaJIT/src"
if [[ ! -d "$LUAJIT_SRC" ]]; then
  echo "Error: LuaJIT source directory not found: $LUAJIT_SRC" >&2
  echo "(Did you forget to init submodules or place LuaJIT under modules/LuaJIT?)" >&2
  exit 1
fi

cd "$LUAJIT_SRC"

if [[ "$OSTYPE" == "darwin"* ]]; then
  echo "Building LuaJIT universal binary (x86_64 + arm64)..."

  # Allow caller to override; default minimum version.
  export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-10.13}"
  CPUs=$(sysctl -n hw.logicalcpu)

  # Remove any previous fat/intermediate libs to avoid accidental reuse.
  rm -f libluajit_x86_64.a libluajit_arm64.a libluajit.a

  # Function to build one architecture.
  build_arch() {
    local ARCH="$1" TARGET_NAME="$2"
    echo "-- Building $ARCH ..."
    make clean || true  # ignore if already clean
    MACOSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET \
    make -j"$CPUs" \
      LUAJIT_T="$TARGET_NAME" \
      BUILDMODE=static \
      CC="clang -arch $ARCH -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
      XCFLAGS="-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
      || { echo "Build failed for $ARCH" >&2; exit 2; }
    mv libluajit.a "libluajit_${ARCH}.a"
    echo "-- Built $ARCH OK"
  }

  build_arch x86_64 luajit-x86_64
  build_arch arm64  luajit-arm64

  echo "-- Creating universal libluajit.a"
  lipo -create -output libluajit.a libluajit_x86_64.a libluajit_arm64.a
  lipo -info libluajit.a || true
  echo "Universal libluajit.a created."
else
  # Linux path
  CPUs="${NPROC:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)}"
  echo "Building LuaJIT static library for Linux with $CPUs threads..."
  make clean || true
  make -j"$CPUs" BUILDMODE=static XCFLAGS="-fPIC"
  echo "Linux libluajit.a built."
fi

echo "Done."
