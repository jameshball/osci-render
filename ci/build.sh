#!/bin/bash -e

PLUGIN="$1"

# Resave jucer file
RESAVE_COMMAND="$PROJUCER_PATH --resave '$ROOT/$PLUGIN.jucer'"
eval "$RESAVE_COMMAND"

# Build mac version
if [ "$OS" = "mac" ]; then
  cd "$ROOT/Builds/$PLUGIN/MacOSX"
  xcodebuild -configuration Release || exit 1

  cp -R "$ROOT/Builds/$PLUGIN/MacOSX/build/Release/$PLUGIN.app" "$ROOT/ci/bin"
  cp -R ~/Library/Audio/Plug-Ins/VST3/$PLUGIN.vst3 "$ROOT/ci/bin"
  cp -R ~/Library/Audio/Plug-Ins/Components/$PLUGIN.component "$ROOT/ci/bin"

  cd "$ROOT/ci/bin"
  
  zip -r ${PLUGIN}-mac.vst3.zip $PLUGIN.vst3
  zip -r ${PLUGIN}-mac.component.zip $PLUGIN.component
  zip -r ${PLUGIN}-mac.app.zip $PLUGIN.app
  cp ${PLUGIN}*.zip "$ROOT/bin"
fi

# Build linux version
if [ "$OS" = "linux" ]; then
  cd "$ROOT/Builds/$PLUGIN/LinuxMakefile"
  make CONFIG=Release

  cp -r ./build/$PLUGIN.vst3 ./build/$PLUGIN "$ROOT/ci/bin"

  cd "$ROOT/ci/bin"

  zip -r ${PLUGIN}-linux-vst3.zip $PLUGIN.vst3
  zip -r ${PLUGIN}-linux.zip $PLUGIN
  cp ${PLUGIN}*.zip "$ROOT/bin"
fi

# Build Win version
if [ "$OS" = "win" ]; then
  VS_WHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  
  MSBUILD_EXE=$("$VS_WHERE" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
  echo $MSBUILD_EXE

  cd "$ROOT/Builds/$PLUGIN/VisualStudio2022"
  "$MSBUILD_EXE" "$PLUGIN.sln" "//p:VisualStudioVersion=16.0" "//m" "//t:Build" "//p:Configuration=Release" "//p:Platform=x64" "//p:PreferredToolArchitecture=x64" "//restore" "//p:RestorePackagesConfig=true"
  cd "$ROOT/ci/bin"
  cp "$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/Standalone Plugin/$PLUGIN.exe" "$ROOT/bin/$PLUGIN-win.exe"
  cp "$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/Standalone Plugin/$PLUGIN.pdb" "$ROOT/bin/$PLUGIN-win.pdb"
  cp -r "$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/VST3/$PLUGIN.vst3/Contents/x86_64-win/$PLUGIN.vst3" "$ROOT/bin/$PLUGIN-win.vst3"
fi

cd "$ROOT"
