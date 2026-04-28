#!/bin/bash -e

PLUGIN="osci-render-test"

# Build LuaJIT before Projucer resave (the test project needs it but can't
# use Projucer postExportShellCommand because of its 60-second timeout)
if [ "$OS" = "win" ]; then
  eval "$($(cygpath "$COMSPEC") /c$(cygpath -w "$ROOT/ci/vcvars_export.bat"))"
  configure_msvc_sccache
  cd "$ROOT/modules/LuaJIT/src"
  cmd //c msvcbuild.bat static
  cp lua51.lib luajit51.lib
  cd "$ROOT"
else
  "$ROOT/luajit_linux_macos.sh"
fi

# Resave jucer file
RESAVE_COMMAND="$PROJUCER_PATH --resave '$ROOT/$PLUGIN.jucer'"
eval "$RESAVE_COMMAND"

# Build mac version
if [ "$OS" = "mac" ]; then
  cd "$ROOT/Builds/Test/MacOSX"
  xcodebuild -configuration Release -parallelizeTargets -jobs $(sysctl -n hw.logicalcpu) || exit 1
  cd "build/Release"
  find .
  echo "Running the test"
  # Run the test
  ./"$PLUGIN"
fi

# Build linux version
if [ "$OS" = "linux" ]; then
  cd "$ROOT/Builds/Test/LinuxMakefile"
  if command -v ccache >/dev/null 2>&1; then
    make -j$(nproc) -e CONFIG=Release CXX="ccache g++" CC="ccache gcc"
  else
    make -j$(nproc) CONFIG=Release
  fi

  cd build
  echo "Running the test"
  # Run the test using the binary
  xvfb-run -a -s "-screen 0 1280x720x24" ./$PLUGIN
fi

# Build Win version
if [ "$OS" = "win" ]; then
  VS_WHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  
  MSBUILD_EXE=$("$VS_WHERE" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
  echo $MSBUILD_EXE

  cd "$ROOT/Builds/Test/VisualStudio2022"
  "$MSBUILD_EXE" "//m" "$PLUGIN.sln" "//p:MultiProcessorCompilation=true" "//p:CL_MPCount=32"  "//p:VisualStudioVersion=16.0" "//t:Build" "//p:Configuration=Release" "//p:Platform=x64" "//p:PreferredToolArchitecture=x64" "${MSBUILD_CACHE_ARGS[@]}"

  if command -v sccache >/dev/null 2>&1; then
    sccache --show-stats || true
  fi
  
  cd "x64/Release/ConsoleApp"
  echo "Running the test"
  ls
  # Run the test using the .exe file
  ./osci-render-test.exe
fi

cd "$ROOT"
