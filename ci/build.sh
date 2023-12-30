#!/bin/bash -e

PLUGIN="osci-render"

# linux specific stiff
if [ $OS = "linux" ]; then
  sudo apt-get update
  sudo apt-get install clang git ladspa-sdk freeglut3-dev g++ libasound2-dev libcurl4-openssl-dev libfreetype6-dev libjack-jackd2-dev libx11-dev libxcomposite-dev libxcursor-dev libxinerama-dev libxrandr-dev mesa-common-dev webkit2gtk-4.0 juce-tools xvfb
fi

ROOT=$(cd "$(dirname "$0")/.."; pwd)
cd "$ROOT"
echo "$ROOT"
rm -Rf bin
mkdir bin

BRANCH=${GITHUB_REF##*/}
echo "$BRANCH"

cd "$ROOT/ci"
rm -Rf bin
mkdir bin

# Get the Projucer
cd "$ROOT/ci/bin"
PROJUCER_OS=$OS
if [ "$OS" = "win" ]; then
  PROJUCER_OS="windows"
elif [ "$OS" = "mac" ]; then
  PROJUCER_OS="osx"
fi

curl -s -S -L "https://api.juce.com/api/v1/download/juce/latest/$PROJUCER_OS" -o Projucer.zip
unzip Projucer.zip

# Set Projucer path based on OS
if [ "$OS" = "mac" ]; then
  PROJUCER_PATH="$ROOT/ci/bin/JUCE/Projucer.app/Contents/MacOS/Projucer"
elif [ "$OS" = "linux" ]; then
  PROJUCER_PATH="$ROOT/ci/bin/JUCE/Projucer"
else
  PROJUCER_PATH="$ROOT/ci/bin/JUCE/Projucer.exe"
fi

# Set global path
GLOBAL_PATH_COMMAND="$PROJUCER_PATH --set-global-search-path $PROJUCER_OS 'defaultJuceModulePath' '$ROOT/ci/bin/JUCE/modules'"
eval "$GLOBAL_PATH_COMMAND"

# Resave jucer file
RESAVE_COMMAND="$PROJUCER_PATH --resave '$ROOT/$PLUGIN.jucer'"
eval "$RESAVE_COMMAND"

# Build mac version
if [ "$OS" = "mac" ]; then
  cd "$ROOT/Builds/MacOSX"
  xcodebuild -configuration Release || exit 1

  cp "$ROOT/Builds/MacOSX/Release/$PLUGIN.app" "$ROOT/ci/bin"
  cp -R ~/Library/Audio/Plug-Ins/VST3/$PLUGIN.vst3 "$ROOT/ci/bin"
  cp -R ~/Library/Audio/Plug-Ins/Components/$PLUGIN.component "$ROOT/ci/bin"

  cd "$ROOT/ci/bin"

  ls
  
  zip -r ${PLUGIN}.vst3.zip $PLUGIN.vst3
  zip -r ${PLUGIN}.app.zip $PLUGIN.component
  zip -r ${PLUGIN}.component.zip $PLUGIN.app
  cp ${PLUGIN}*.zip "$ROOT/bin"
fi

# Build linux version
if [ "$OS" = "linux" ]; then
  cd "$ROOT/Builds/LinuxMakefile"
  make CONFIG=Release

  cp -r ./build/$PLUGIN.vst3 ./build/$PLUGIN "$ROOT/ci/bin"

  cd "$ROOT/ci/bin"

  zip -r ${PLUGIN}-vst3.zip $PLUGIN.vst3
  zip -r ${PLUGIN}.zip $PLUGIN
  cp ${PLUGIN}*.zip "$ROOT/bin"
fi

# Build Win version
if [ "$OS" = "win" ]; then
  VS_WHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  
  MSBUILD_EXE=$("$VS_WHERE" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
  echo $MSBUILD_EXE

  cd "$ROOT/Builds/VisualStudio2022"
  "$MSBUILD_EXE" "$PLUGIN.sln" "//p:VisualStudioVersion=16.0" "//m" "//t:Build" "//p:Configuration=Release" "//p:Platform=x64" "//p:PreferredToolArchitecture=x64"
  echo "Build done"

  cd "$ROOT/ci/bin"

  echo "Copy VST3"
  ls "$ROOT/Builds/VisualStudio2022/x64/Release/"
  ls "$ROOT/Builds/VisualStudio2022/x64/Release/VST3/"
  cp "$ROOT/Builds/VisualStudio2022/x64/Release/VST3/$PLUGIN.vst3" "$ROOT/bin"
fi
