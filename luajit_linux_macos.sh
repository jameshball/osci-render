#!/bin/sh

# Run this if you're on Linux and you haven't already built LuaJIT.
# If you don't, osci-render won't compile.

cd ..\..\..\modules\LuaJIT\src
make -j$(nproc)