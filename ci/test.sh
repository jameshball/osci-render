#!/bin/bash -e

PLUGIN="osci-render-test"

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
  make -J$(nproc) CONFIG=Release

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
  "$MSBUILD_EXE" "$PLUGIN.sln" "//p:VisualStudioVersion=16.0" "//m" "//t:Build" "//p:Configuration=Release" "//p:Platform=x64" "//p:PreferredToolArchitecture=x64" -maxcpucount
  
  cd "x64/Release/ConsoleApp"
  echo "Running the test"
  ls
  # Run the test using the .exe file
  ./osci-render-test.exe
fi

cd "$ROOT"
