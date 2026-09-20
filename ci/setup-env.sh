#!/bin/bash -e

# linux specific stiff
if [ "$OS" = "linux" ]; then
  sudo apt-get update
  sudo apt-get install -y clang cmake git ladspa-sdk freeglut3-dev g++ libasound2-dev libcurl4-openssl-dev libfreetype6-dev libjack-jackd2-dev libx11-dev libxext-dev libxcomposite-dev libxcursor-dev libxinerama-dev libxrandr-dev mesa-common-dev libegl1-mesa-dev libwebkit2gtk-4.1-dev juce-tools xvfb
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
if [ "$OS" = "linux" ] && [ "$(uname -m)" = "aarch64" ]; then
  curl -f -s -S -L --retry 3 --retry-delay 5 "https://github.com/juce-framework/JUCE/archive/refs/tags/$JUCE_TAG.zip" -o JUCE-source.zip
  unzip -q JUCE-source.zip
  mv "JUCE-$JUCE_TAG" JUCE
  cmake -S JUCE -B JUCE/build -DJUCE_BUILD_EXTRAS=ON -DJUCE_BUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build JUCE/build --target Projucer --parallel "$(nproc)"
else
  curl -f -s -S -L --retry 3 --retry-delay 5 "https://github.com/juce-framework/JUCE/releases/download/$JUCE_TAG/juce-${JUCE_TAG}-$PROJUCER_OS.zip" -o Projucer.zip
  unzip -q Projucer.zip
fi

# Set Projucer path based on OS
if [ "$OS" = "mac" ]; then
  PROJUCER_PATH="$ROOT/ci/bin/JUCE/Projucer.app/Contents/MacOS/Projucer"
elif [ "$OS" = "linux" ]; then
  if [ "$(uname -m)" = "aarch64" ]; then
    PROJUCER_PATH=$(find "$ROOT/ci/bin/JUCE" -type f -name Projucer -perm -111 -print -quit)
    if [ -z "$PROJUCER_PATH" ]; then
      echo "Native ARM64 Projucer build was not found"
      exit 1
    fi
  else
    PROJUCER_PATH="$ROOT/ci/bin/JUCE/Projucer"
  fi
else
  PROJUCER_PATH="$ROOT/ci/bin/JUCE/Projucer.exe"
fi

# Set global path
GLOBAL_PATH_COMMAND="$PROJUCER_PATH --set-global-search-path $PROJUCER_OS 'defaultJuceModulePath' '$ROOT/ci/bin/JUCE/modules'"
eval "$GLOBAL_PATH_COMMAND"

cd "$ROOT"
