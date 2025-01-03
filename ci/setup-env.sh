#!/bin/bash -e

ls -R

# linux specific stiff
if [ $OS = "linux" ]; then
  sudo apt-get update
  sudo apt-get install clang git ladspa-sdk freeglut3-dev g++ libasound2-dev libcurl4-openssl-dev libfreetype6-dev libjack-jackd2-dev libx11-dev libxcomposite-dev libxcursor-dev libxinerama-dev libxrandr-dev mesa-common-dev webkit2gtk-4.0 juce-tools xvfb
fi

ROOT=$(pwd)
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

# Get the Spout SDK and SpoutLibrary.dll
if [ "$OS" = "win" ]; then
  curl -s -S -L https://github.com/leadedge/Spout2/releases/download/2.007.015/Spout-SDK-binaries_2-007-015.zip -o Spout.zip
  unzip Spout.zip
  ls -R
  # move to system32
  cp Spout-SDK-binaries/2-007-015/Libs/MD/bin/SpoutLibrary.dll /c/Windows/System32
fi

# Set global path
GLOBAL_PATH_COMMAND="$PROJUCER_PATH --set-global-search-path $PROJUCER_OS 'defaultJuceModulePath' '$ROOT/ci/bin/JUCE/modules'"
eval "$GLOBAL_PATH_COMMAND"

cd "$ROOT"
