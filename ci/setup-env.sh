#!/bin/bash -e

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

CURL_OPTS=(-s -S -L)
if [ -n "$GITHUB_TOKEN" ]; then
  CURL_OPTS+=(-H "Authorization: Bearer $GITHUB_TOKEN")
fi
JUCE_TAG=$(curl "${CURL_OPTS[@]}" "https://api.github.com/repos/juce-framework/JUCE/releases/latest" | grep '"tag_name"' | head -1 | sed 's/.*"tag_name": *"\([^"]*\)".*/\1/' || true)
if [ -z "$JUCE_TAG" ]; then
  echo "Warning: Could not determine latest JUCE version from GitHub API, using fallback"
  JUCE_TAG="8.0.12"
fi
echo "Latest JUCE release: $JUCE_TAG"
curl -f -s -S -L --retry 3 --retry-delay 5 "https://github.com/juce-framework/JUCE/releases/download/$JUCE_TAG/juce-${JUCE_TAG}-$PROJUCER_OS.zip" -o Projucer.zip
unzip -q Projucer.zip

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
  cp "$ROOT/External/spout/SpoutLibrary.dll" /c/Windows/System32
elif [ "$OS" = "mac" ]; then
  sudo cp -R "$ROOT/External/syphon/Syphon.framework" "/Library/Frameworks"
fi

# Set global path
GLOBAL_PATH_COMMAND="$PROJUCER_PATH --set-global-search-path $PROJUCER_OS 'defaultJuceModulePath' '$ROOT/ci/bin/JUCE/modules'"
eval "$GLOBAL_PATH_COMMAND"

cd "$ROOT"
