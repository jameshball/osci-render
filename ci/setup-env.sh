#!/bin/bash -e

# Linux build dependencies are installed by `awalsh128/cache-apt-pkgs-action`
# in the workflow, but keep a fallback so direct callers of this script still
# get a complete environment on fresh Ubuntu/Debian systems.
if [ "$OS" = "linux" ]; then
  LINUX_PACKAGES=(
    ccache ninja-build clang git ladspa-sdk freeglut3-dev g++
    libasound2-dev libcurl4-openssl-dev libfreetype6-dev libjack-jackd2-dev
    libx11-dev libxcomposite-dev libxcursor-dev libxinerama-dev libxrandr-dev
    mesa-common-dev webkit2gtk-4.0 juce-tools xvfb
  )

  if command -v apt-get >/dev/null 2>&1 && command -v dpkg-query >/dev/null 2>&1; then
    MISSING_PACKAGES=()
    for package in "${LINUX_PACKAGES[@]}"; do
      if ! dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed'; then
        MISSING_PACKAGES+=("$package")
      fi
    done

    if [ "${#MISSING_PACKAGES[@]}" -gt 0 ]; then
      sudo apt-get update
      sudo apt-get install -y "${MISSING_PACKAGES[@]}"
    fi
  else
    echo "Skipping Linux package install fallback: apt-get/dpkg-query not available."
  fi
fi

# macOS-specific tooling (ccache for compile cache, ninja for pluginval CMake build)
if [ "$OS" = "mac" ]; then
  if ! command -v ccache >/dev/null 2>&1; then
    brew install ccache
  fi
  if ! command -v ninja >/dev/null 2>&1; then
    brew install ninja
  fi
fi

# Configure ccache for JUCE/Projucer builds. The "sloppiness" flags allow PCH and
# time/file macros to hit the cache; without them most TUs are reported uncacheable.
if command -v ccache >/dev/null 2>&1; then
  ccache --set-config sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime
  ccache --set-config max_size=2G
  ccache -z >/dev/null 2>&1 || true
fi

ROOT=$(pwd)
echo "$ROOT"
rm -Rf bin
mkdir bin

MSBUILD_CACHE_ARGS=()

configure_msvc_sccache() {
  MSBUILD_CACHE_ARGS=()

  if [ "$OS" != "win" ] || ! command -v sccache >/dev/null 2>&1; then
    return 0
  fi

  if ! command -v cl.exe >/dev/null 2>&1; then
    echo "Skipping Windows sccache wrapper: cl.exe is not on PATH yet."
    return 0
  fi

  SCCACHE_MSVC_DIR="$ROOT/ci/bin/sccache-msvc"
  mkdir -p "$SCCACHE_MSVC_DIR"
  cp "$(command -v sccache)" "$SCCACHE_MSVC_DIR/sccache.exe"

  cat > "$SCCACHE_MSVC_DIR/cl-wrapper.c" <<'EOF'
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int main(int argc, char** argv)
{
    char modulePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, modulePath, MAX_PATH) == 0)
        return 1;

    char selfPath[MAX_PATH];
    strncpy(selfPath, modulePath, sizeof(selfPath));
    selfPath[sizeof(selfPath) - 1] = '\0';

    char* slash = strrchr(modulePath, '\\');
    if (slash != NULL)
        slash[1] = '\0';
    else
        modulePath[0] = '\0';

    char sccachePath[MAX_PATH];
    snprintf(sccachePath, sizeof(sccachePath), "%ssccache.exe", modulePath);

    char realClPath[MAX_PATH] = "cl.exe";
    char* pathEnv = getenv("PATH");
    if (pathEnv != NULL)
    {
      char* paths = _strdup(pathEnv);
      if (paths != NULL)
      {
        char* context = NULL;
        for (char* dir = strtok_s(paths, ";", &context); dir != NULL; dir = strtok_s(NULL, ";", &context))
        {
          char candidate[MAX_PATH];
          snprintf(candidate, sizeof(candidate), "%s\\cl.exe", dir);
          DWORD attrs = GetFileAttributesA(candidate);
          if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 && _stricmp(candidate, selfPath) != 0)
          {
            strncpy(realClPath, candidate, sizeof(realClPath));
            realClPath[sizeof(realClPath) - 1] = '\0';
            break;
          }
        }
        free(paths);
      }
    }

    char** childArgs = (char**) calloc((size_t) argc + 2, sizeof(char*));
    if (childArgs == NULL)
        return 1;

    childArgs[0] = sccachePath;
    childArgs[1] = realClPath;
    for (int i = 1; i < argc; ++i)
        childArgs[i + 1] = argv[i];
    childArgs[argc + 1] = NULL;

    int result = _spawnv(_P_WAIT, sccachePath, childArgs);
    if (result == -1)
    {
        perror("sccache cl.exe");
        free(childArgs);
        return 1;
    }

    free(childArgs);
    return result;
}
EOF

  WRAPPER_EXE_WIN=$(cygpath -w "$SCCACHE_MSVC_DIR/cl.exe")
  WRAPPER_OBJ_WIN=$(cygpath -w "$SCCACHE_MSVC_DIR/cl-wrapper.obj")
  WRAPPER_SOURCE_WIN=$(cygpath -w "$SCCACHE_MSVC_DIR/cl-wrapper.c")
  cl.exe -nologo -O2 -Fe"$WRAPPER_EXE_WIN" -Fo"$WRAPPER_OBJ_WIN" "$WRAPPER_SOURCE_WIN"

  MSBUILD_CACHE_ARGS=(
    "//p:TrackFileAccess=false"
    "//p:UseMultiToolTask=true"
    "//p:CLToolExe=cl.exe"
    "//p:CLToolPath=$(cygpath -w "$SCCACHE_MSVC_DIR")"
  )
  echo "Using sccache for Windows MSVC builds: $SCCACHE_MSVC_DIR/cl.exe"
}

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
