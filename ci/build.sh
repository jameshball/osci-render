#!/bin/bash -e

PLUGIN="$1"
VERSION="$2"

OUTPUT_NAME="$PLUGIN-$VERSION"

# If we are on the free version, we need to disable the build flag SOSCI_FEATURES
if [ "$VERSION" = "free" ]; then
  # Edit the jucer file to disable the SOSCI_FEATURES flag
  if [ "$OS" = "mac" ]; then
    sed -i '' 's/SOSCI_FEATURES=1/SOSCI_FEATURES=0/' "$ROOT/$PLUGIN.jucer"
  else
    sed -i 's/SOSCI_FEATURES=1/SOSCI_FEATURES=0/' "$ROOT/$PLUGIN.jucer"
  fi
fi

# Resave jucer file
RESAVE_COMMAND="$PROJUCER_PATH --resave '$ROOT/$PLUGIN.jucer'"
eval "$RESAVE_COMMAND"

# Build mac version
if [ "$OS" = "mac" ]; then
  cd "$ROOT/Builds/$PLUGIN/MacOSX"
  xcodebuild -configuration Release || exit 1
fi

# Build linux version
if [ "$OS" = "linux" ]; then
  cd "$ROOT/Builds/$PLUGIN/LinuxMakefile"
  make CONFIG=Release

  cp -r ./build/$PLUGIN.vst3 "$ROOT/ci/bin/$PLUGIN.vst3"
  cp -r ./build/$PLUGIN "$ROOT/ci/bin/$PLUGIN"

  cd "$ROOT/ci/bin"

  zip -r ${OUTPUT_NAME}-linux-vst3.zip $PLUGIN.vst3
  zip -r ${OUTPUT_NAME}-linux.zip $PLUGIN
  cp ${OUTPUT_NAME}*.zip "$ROOT/bin"
fi

# Build Win version
if [ "$OS" = "win" ]; then
  VS_WHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  
  MSBUILD_EXE=$("$VS_WHERE" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
  echo $MSBUILD_EXE

  cd "$ROOT/Builds/$PLUGIN/VisualStudio2022"
  "$MSBUILD_EXE" "$PLUGIN.sln" "//p:VisualStudioVersion=16.0" "//m" "//t:Build" "//p:Configuration=Release" "//p:Platform=x64" "//p:PreferredToolArchitecture=x64" "//restore" "//p:RestorePackagesConfig=true"
  cp "$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/Standalone Plugin/$PLUGIN.pdb" "$ROOT/bin/$OUTPUT_NAME.pdb"
fi

cd "$ROOT"
