#!/bin/bash -e

PLUGIN="$1"
VERSION="$2"

# sosci has only one variant, so we omit the version suffix from its artifact names
if [ "$PLUGIN" = "sosci" ]; then
  OUTPUT_NAME="$PLUGIN"
else
  OUTPUT_NAME="$PLUGIN-$VERSION"
fi

# Platform-aware in-place sed
sed_jucer() {
  if [ "$OS" = "mac" ]; then
    sed -i '' "$1" "$ROOT/$PLUGIN.jucer"
  else
    sed -i "$1" "$ROOT/$PLUGIN.jucer"
  fi
}

# Build for the current platform.
# Pass --clean to run `make clean` first (Linux only, used for instrument build
# to avoid stale objects from an earlier differently-named target).
platform_build() {
  local clean="${1:-}"

  if [ "$OS" = "mac" ]; then
    cd "$ROOT/Builds/$PLUGIN/MacOSX"
    xcodebuild -configuration Release -parallelizeTargets -jobs $(sysctl -n hw.logicalcpu) || exit 1
  fi

  if [ "$OS" = "linux" ]; then
    cd "$ROOT/Builds/$PLUGIN/LinuxMakefile"
    if [ "$clean" = "--clean" ]; then make clean CONFIG=Release; fi
    # Use ccache as a compiler launcher when available (CI installs it via setup-env).
    # Pass via -e so make uses our value even though the JUCE-generated Makefile sets CXX itself.
    if command -v ccache >/dev/null 2>&1; then
      make -j$(nproc) -e CONFIG=Release CXX="ccache g++" CC="ccache gcc"
    else
      make -j$(nproc) CONFIG=Release
    fi
  fi

  if [ "$OS" = "win" ]; then
    cd "$ROOT/Builds/$PLUGIN/VisualStudio2022"
    msbuild.exe "//m" "$PLUGIN.sln" \
      "//p:VisualStudioVersion=16.0" \
      "//p:MultiProcessorCompilation=true" \
      "//p:CL_MPCount=16" \
      "//p:BuildInParallel=true" \
      "//t:Build" \
      "//p:Configuration=Release" \
      "//p:Platform=x64" \
      "//p:PreferredToolArchitecture=x64" \
      "//restore" \
      "//p:RestorePackagesConfig=true"
  fi
}

# If we are on the free version, we need to disable the build flag OSCI_PREMIUM
if [ "$VERSION" = "free" ]; then
  # Edit the jucer file to disable the OSCI_PREMIUM flag
  sed_jucer 's/OSCI_PREMIUM=1/OSCI_PREMIUM=0/'
fi

# Resave jucer file
RESAVE_COMMAND="$PROJUCER_PATH --resave '$ROOT/$PLUGIN.jucer'"
eval "$RESAVE_COMMAND"

# Set up Windows build environment before any msbuild invocation
if [ "$OS" = "win" ]; then
  VS_WHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  eval "$($(cygpath "$COMSPEC") /c$(cygpath -w "$ROOT/ci/vcvars_export.bat"))"
fi

# Build effect version
platform_build

if [ "$OS" = "linux" ]; then
  cp -r "$ROOT/Builds/$PLUGIN/LinuxMakefile/build/$PLUGIN.vst3" "$ROOT/ci/bin/$PLUGIN.vst3"
  cp -r "$ROOT/Builds/$PLUGIN/LinuxMakefile/build/$PLUGIN"      "$ROOT/ci/bin/$PLUGIN"
fi

if [ "$OS" = "win" ]; then
  cp "$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/Standalone Plugin/$PLUGIN.pdb" "$ROOT/bin/$OUTPUT_NAME.pdb"
fi

# Build instrument version (osci-render only)
if [ "$PLUGIN" = "osci-render" ]; then
  cd "$ROOT"
  INSTRUMENT_TARGET="$PLUGIN-instrument"

  echo "============================================="
  echo " Building instrument version..."
  echo "============================================="

  # Save effect build artifacts before regenerating the project
  # (Xcode/MSBuild may clean old products when target names change)
  EFFECT_SAVE_DIR="$ROOT/ci/bin/effect-save"
  mkdir -p "$EFFECT_SAVE_DIR"

  if [ "$OS" = "mac" ]; then
    MAC_BUILD="$ROOT/Builds/$PLUGIN/MacOSX/build/Release"
    cp -R "$MAC_BUILD/$PLUGIN.vst3"      "$EFFECT_SAVE_DIR/"
    cp -R "$MAC_BUILD/$PLUGIN.component" "$EFFECT_SAVE_DIR/"
    cp -R "$MAC_BUILD/$PLUGIN.app"       "$EFFECT_SAVE_DIR/"
  fi

  if [ "$OS" = "win" ]; then
    WIN_BUILD="$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release"
    cp -R "$WIN_BUILD/VST3/$PLUGIN.vst3"              "$EFFECT_SAVE_DIR/"
    cp -R "$WIN_BUILD/Standalone Plugin/$PLUGIN.exe"  "$EFFECT_SAVE_DIR/"
  fi

  # Add pluginIsSynth to characteristics
  sed_jucer 's/pluginCharacteristicsValue="pluginWantsMidiIn"/pluginCharacteristicsValue="pluginWantsMidiIn,pluginIsSynth"/'

  # Change AU main type from music effect to music device (instrument)
  sed_jucer "s/pluginAUMainType=\"'aumf'\"/pluginAUMainType=\"'aumu'\"/"

  # Add explicit pluginName (DAW display name) and pluginCode for the instrument variant
  # pluginName keeps the user-facing "(instrument)" suffix; targetName uses hyphen for filesystem safety
  sed_jucer 's/name="osci-render" projectType/name="osci-render" pluginName="osci-render (instrument)" pluginCode="Hh2i" projectType/'

  # Change all targetName occurrences to produce differently-named binaries
  # Uses hyphen instead of parentheses to avoid Linux Makefile quoting issues
  sed_jucer 's/targetName="osci-render"/targetName="osci-render-instrument"/g'

  # Resave jucer file with instrument settings and build
  eval "$RESAVE_COMMAND"
  platform_build --clean

  if [ "$OS" = "linux" ]; then
    cp -r "$ROOT/Builds/$PLUGIN/LinuxMakefile/build/$INSTRUMENT_TARGET.vst3" "$ROOT/ci/bin/$INSTRUMENT_TARGET.vst3"
  fi

  # Restore effect build artifacts alongside the instrument ones
  if [ "$OS" = "mac" ]; then
    MAC_BUILD="$ROOT/Builds/$PLUGIN/MacOSX/build/Release"
    cp -R "$EFFECT_SAVE_DIR/$PLUGIN.vst3"      "$MAC_BUILD/"
    cp -R "$EFFECT_SAVE_DIR/$PLUGIN.component" "$MAC_BUILD/"
    cp -R "$EFFECT_SAVE_DIR/$PLUGIN.app"       "$MAC_BUILD/"
  fi

  if [ "$OS" = "win" ]; then
    WIN_BUILD="$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release"
    cp -R "$EFFECT_SAVE_DIR/$PLUGIN.vst3"             "$WIN_BUILD/VST3/"
    cp -R "$EFFECT_SAVE_DIR/$PLUGIN.exe"              "$WIN_BUILD/Standalone Plugin/"
  fi
fi

# Package linux binaries (after both effect and instrument builds)
# Produces a single zip containing the standalone executable + vst3(s) at the root.
if [ "$OS" = "linux" ]; then
  cd "$ROOT/ci/bin"

  if [ "$PLUGIN" = "osci-render" ]; then
    INSTRUMENT_TARGET="$PLUGIN-instrument"
    zip -r "${OUTPUT_NAME}-linux.zip" "$PLUGIN" "$PLUGIN.vst3" "$INSTRUMENT_TARGET.vst3"
  else
    zip -r "${OUTPUT_NAME}-linux.zip" "$PLUGIN" "$PLUGIN.vst3"
  fi
  cp "${OUTPUT_NAME}-linux.zip" "$ROOT/bin"
fi

cd "$ROOT"
