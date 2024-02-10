#!/bin/bash -e

PLUGIN="osci-render-test"

# Resave jucer file
RESAVE_COMMAND="$PROJUCER_PATH --resave '$ROOT/$PLUGIN.jucer'"
eval "$RESAVE_COMMAND"

# Build mac version
if [ "$OS" = "mac" ]; then
  cd "$ROOT/Builds/Test/MacOSX"
  xcodebuild -configuration Release || exit 1
  cd "build/Release"
  # Run the test using the .app file
  open -a "$PLUGIN.app"
fi

# Build linux version
if [ "$OS" = "linux" ]; then
  cd "$ROOT/Builds/LinuxMakefile"
  make CONFIG=Release

  cd build
  # Run the test using the binary
  xvfb-run -a -s "-screen 0 1280x720x24" ./$PLUGIN
fi

# Build Win version
if [ "$OS" = "win" ]; then
  VS_WHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  
  MSBUILD_EXE=$("$VS_WHERE" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
  echo $MSBUILD_EXE

  cd "$ROOT/Builds/VisualStudio2022"
  "$MSBUILD_EXE" "$PLUGIN.sln" "//p:VisualStudioVersion=16.0" "//m" "//t:Build" "//p:Configuration=Release" "//p:Platform=x64" "//p:PreferredToolArchitecture=x64"
  
  cd "x64/Release/Standalone Plugin"
  # Run the test using the .exe file
  ./$PLUGIN.exe
fi

cd "$ROOT"
