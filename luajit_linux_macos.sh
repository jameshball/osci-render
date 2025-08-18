#!/bin/bash

# Run this if you're on Linux or macOS and you haven't already built LuaJIT.
# If you don't, osci-render won't compile.

cd "$DIR/modules/LuaJIT/src" || exit 1

if [[ "$OSTYPE" == *"darwin"* ]]; then
  echo "Building LuaJIT universal binary (x86_64 + arm64)..."
  
  make clean
  
  export MACOSX_DEPLOYMENT_TARGET=10.13
  
  # build x86_64
  make -j$(sysctl -n hw.logicalcpu) \
    LUAJIT_T=luajit-x86_64 \
    BUILDMODE=static \
    CC='clang -target x86_64-apple-macos' \
	|| exit 2
	
  mv libluajit.a libluajit_x86_64.a
  
  make clean
  
  # Build arm64
  make -j$(sysctl -n hw.logicalcpu) \
    LUAJIT_T=luajit-arm64 \
    BUILDMODE=static \
    CC='clang -target arm64-apple-macos' \
	|| exit 3

  mv libluajit.a libluajit_arm64.a
  
  # Merge into fat binary
  lipo -create -output libluajit.a libluajit_x86_64.a libluajit_arm64.a
  echo "Universal libluajit.a created."
  
else
  echo "Building LuaJIT for Linux..."
  make -j$(nproc) BUILDMODE=static XCFLAGS="-fPIC"
fi
