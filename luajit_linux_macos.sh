#!/bin/sh

# Run this if you're on Linux and you haven't already built LuaJIT.
# If you don't, osci-render won't compile.

cd "$DIR/modules/LuaJIT/src"

if [[ "$OSTYPE" == "darwin"* ]]; then
  make -j$(sysctl -n hw.logicalcpu) BUILDMODE=static MACOSX_DEPLOYMENT_TARGET=10.13
else
  make -j$(nproc) BUILDMODE=static
fi
