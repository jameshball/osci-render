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

  cp -R "$ROOT/Builds/$PLUGIN/MacOSX/build/Release/$PLUGIN.app" "$ROOT/ci/bin/$OUTPUT_NAME.app"
  cp -R ~/Library/Audio/Plug-Ins/VST3/$PLUGIN.vst3 "$ROOT/ci/bin/$OUTPUT_NAME.vst3"
  cp -R ~/Library/Audio/Plug-Ins/Components/$PLUGIN.component "$ROOT/ci/bin/$OUTPUT_NAME.component"

  cd "$ROOT/ci/bin"
  
  zip -r ${OUTPUT_NAME}-mac.vst3.zip $OUTPUT_NAME.vst3
  zip -r ${OUTPUT_NAME}-mac.component.zip $OUTPUT_NAME.component
  zip -r ${OUTPUT_NAME}-mac.app.zip $OUTPUT_NAME.app
  cp ${OUTPUT_NAME}*.zip "$ROOT/bin"
fi

# Build linux version
if [ "$OS" = "linux" ]; then
  cd "$ROOT/Builds/$PLUGIN/LinuxMakefile"
  make CONFIG=Release

  cp -r ./build/$PLUGIN.vst3 "$ROOT/ci/bin/$OUTPUT_NAME.vst3"
  cp -r ./build/$PLUGIN "$ROOT/ci/bin/$OUTPUT_NAME"

  cd "$ROOT/ci/bin"

  zip -r ${OUTPUT_NAME}-linux-vst3.zip $OUTPUT_NAME.vst3
  zip -r ${OUTPUT_NAME}-linux.zip $OUTPUT_NAME
  cp ${OUTPUT_NAME}*.zip "$ROOT/bin"
fi

# Build Win version
if [ "$OS" = "win" ]; then
  VS_WHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  
  MSBUILD_EXE=$("$VS_WHERE" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
  echo $MSBUILD_EXE

  cd "$ROOT/Builds/$PLUGIN/VisualStudio2022"
  "$MSBUILD_EXE" "$PLUGIN.sln" "//p:VisualStudioVersion=16.0" "//m" "//t:Build" "//p:Configuration=Release" "//p:Platform=x64" "//p:PreferredToolArchitecture=x64" "//restore" "//p:RestorePackagesConfig=true"
  cd "$ROOT/ci/bin"
  cp "$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/Standalone Plugin/$PLUGIN.exe" "$ROOT/bin/$OUTPUT_NAME-win.exe"
  cp "$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/Standalone Plugin/$PLUGIN.pdb" "$ROOT/bin/$OUTPUT_NAME-win.pdb"
  cp -r "$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/VST3/$PLUGIN.vst3/Contents/x86_64-win/$PLUGIN.vst3" "$ROOT/bin/$OUTPUT_NAME-win.vst3"
fi

cd "$ROOT"
