#!/bin/bash

# Build LuaJIT for Linux or macOS.
# On macOS this produces a universal static lib and only rebuilds stale architectures.
# On Linux this skips work unless sources or build configuration changed.

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

source_is_newer_than() {
  local target="$1"

  if [[ ! -f "$target" ]]; then
    return 0
  fi

  find . -type f \( \
    -name '*.c' -o \
    -name '*.h' -o \
    -name '*.S' -o \
    -name '*.dasc' -o \
    -name 'Makefile' -o \
    -name 'Makefile.dep' \
  \) \
  ! -path './host/*' \
  ! -path './jit/vmdef.lua' \
  ! -name 'luajit.h' \
  ! -name 'luajit_relver.txt' \
  ! -name 'lj_vm.S' \
  ! -name 'lj_bcdef.h' \
  ! -name 'lj_ffdef.h' \
  ! -name 'lj_libdef.h' \
  ! -name 'lj_recdef.h' \
  ! -name 'lj_folddef.h' \
  ! -name 'buildvm_arch.h' \
  -newer "$target" -print -quit | grep -q .
}

stamp_matches() {
  local stamp_path="$1" expected_value="$2"

  [[ -f "$stamp_path" ]] && [[ "$(<"$stamp_path")" == "$expected_value" ]]
}

write_stamp() {
  local stamp_path="$1" stamp_value="$2"
  printf '%s\n' "$stamp_value" > "$stamp_path"
}

if [[ "$OSTYPE" == "darwin"* ]]; then
  echo "Ensuring LuaJIT universal binary (x86_64 + arm64)..."

  # Allow caller to override; default minimum version.
  export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-10.13}"
  CPUs=$(sysctl -n hw.logicalcpu)
  BUILD_CONFIG="darwin|MACOSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET"
  X86_STAMP=".luajit-build-x86_64.stamp"
  ARM_STAMP=".luajit-build-arm64.stamp"

  should_build_arch() {
    local arch="$1" lib_path="$2" stamp_path="$3"

    if [[ ! -f "$lib_path" ]]; then
      return 0
    fi

    if ! stamp_matches "$stamp_path" "$BUILD_CONFIG"; then
      return 0
    fi

    if source_is_newer_than "$lib_path"; then
      return 0
    fi

    return 1
  }

  should_recreate_universal() {
    [[ ! -f libluajit.a ]] && return 0
    [[ libluajit_x86_64.a -nt libluajit.a ]] && return 0
    [[ libluajit_arm64.a -nt libluajit.a ]] && return 0
    return 1
  }

  # Function to build one architecture.
  build_arch() {
    local ARCH="$1" TARGET_NAME="$2" STAMP_PATH="$3"
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
    write_stamp "$STAMP_PATH" "$BUILD_CONFIG"
    echo "-- Built $ARCH OK"
  }

  rebuilt_any_arch=0

  if should_build_arch x86_64 libluajit_x86_64.a "$X86_STAMP"; then
    build_arch x86_64 luajit-x86_64 "$X86_STAMP"
    rebuilt_any_arch=1
  else
    echo "-- x86_64 build is up to date"
  fi

  if should_build_arch arm64 libluajit_arm64.a "$ARM_STAMP"; then
    build_arch arm64 luajit-arm64 "$ARM_STAMP"
    rebuilt_any_arch=1
  else
    echo "-- arm64 build is up to date"
  fi

  if [[ ! -f libluajit_x86_64.a || ! -f libluajit_arm64.a ]]; then
    echo "Missing per-architecture LuaJIT library after build." >&2
    exit 3
  fi

  if [[ "$rebuilt_any_arch" -eq 1 ]] || should_recreate_universal; then
    echo "-- Creating universal libluajit.a"
    rm -f libluajit.a
    lipo -create -output libluajit.a libluajit_x86_64.a libluajit_arm64.a
    lipo -info libluajit.a || true
    echo "Universal libluajit.a created."
  else
    echo "-- Universal libluajit.a is up to date"
  fi
else
  # Linux path
  CPUs="${NPROC:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)}"
  BUILD_CONFIG="linux|XCFLAGS=-fPIC"
  LINUX_STAMP=".luajit-build-linux.stamp"

  if source_is_newer_than libluajit.a || ! stamp_matches "$LINUX_STAMP" "$BUILD_CONFIG"; then
    echo "Building LuaJIT static library for Linux with $CPUs threads..."
    make clean || true
    make -j"$CPUs" BUILDMODE=static XCFLAGS="-fPIC"
    write_stamp "$LINUX_STAMP" "$BUILD_CONFIG"
    echo "Linux libluajit.a built."
  else
    echo "Linux libluajit.a is up to date."
  fi
fi

echo "Done."
