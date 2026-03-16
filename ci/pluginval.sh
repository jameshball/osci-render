#!/bin/bash -e

# pluginval validation script
# Usage (CI):   source ./ci/pluginval.sh "osci-render"
# Usage (local): ROOT=$(pwd) OS=win ./ci/pluginval.sh "osci-render"
#
# Requires: cmake, a C++ toolchain, and a previously-built plugin.

PLUGIN="${1:?Usage: pluginval.sh <plugin-name>}"
STRICTNESS="${PLUGINVAL_STRICTNESS:-5}"
SKIP_GUI="${PLUGINVAL_SKIP_GUI:-}"

# ── Resolve paths ──────────────────────────────────────────────

ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
PLUGINVAL_SRC="$ROOT/modules/pluginval"
PLUGINVAL_BUILD="$PLUGINVAL_SRC/Builds"

# ── Detect OS if not already set ───────────────────────────────

if [ -z "$OS" ]; then
    case "$(uname -s)" in
        Darwin*) OS="mac"  ;;
        Linux*)  OS="linux" ;;
        MINGW*|MSYS*|CYGWIN*) OS="win" ;;
        *)       echo "Unknown platform"; exit 1 ;;
    esac
fi

# ── Build pluginval from source ────────────────────────────────

echo "============================================="
echo " Building pluginval from source..."
echo "============================================="

cd "$PLUGINVAL_SRC"

# pluginval_IS_TOP_LEVEL is auto-set by CMake >= 3.21; force it for older versions
CMAKE_EXTRA_ARGS="-Dpluginval_IS_TOP_LEVEL=ON"

if [ "$OS" = "win" ]; then
    # Windows: use Visual Studio generator
    cmake -B Builds -DCMAKE_BUILD_TYPE=Release -DPLUGINVAL_VST3_VALIDATOR=OFF $CMAKE_EXTRA_ARGS .
    cmake --build Builds --config Release --parallel
else
    cmake -B Builds -DCMAKE_BUILD_TYPE=Release -DPLUGINVAL_VST3_VALIDATOR=OFF $CMAKE_EXTRA_ARGS .
    cmake --build Builds --config Release --parallel
fi

# ── Find the pluginval binary ──────────────────────────────────

if [ "$OS" = "mac" ]; then
    PLUGINVAL="$PLUGINVAL_BUILD/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval"
elif [ "$OS" = "linux" ]; then
    PLUGINVAL="$PLUGINVAL_BUILD/pluginval_artefacts/Release/pluginval"
else
    PLUGINVAL="$PLUGINVAL_BUILD/pluginval_artefacts/Release/pluginval.exe"
fi

if [ ! -f "$PLUGINVAL" ]; then
    echo "ERROR: pluginval binary not found at $PLUGINVAL"
    echo "Searching for it..."
    find "$PLUGINVAL_BUILD" -name "pluginval*" -type f 2>/dev/null || true
    exit 1
fi

echo "pluginval binary: $PLUGINVAL"

# ── Locate the built VST3 plugin ──────────────────────────────

if [ "$OS" = "mac" ]; then
    VST3_PATH="$ROOT/Builds/$PLUGIN/MacOSX/build/Release/$PLUGIN.vst3"
elif [ "$OS" = "linux" ]; then
    VST3_PATH="$ROOT/Builds/$PLUGIN/LinuxMakefile/build/$PLUGIN.vst3"
else
    # Try Release first, then Debug
    VST3_PATH="$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/VST3/$PLUGIN.vst3"
    if [ ! -d "$VST3_PATH" ]; then
        VST3_PATH="$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Debug/VST3/$PLUGIN.vst3"
    fi
fi

if [ ! -d "$VST3_PATH" ]; then
    echo "ERROR: VST3 plugin not found at $VST3_PATH"
    echo "Make sure you have built the plugin first."
    exit 1
fi

echo "VST3 plugin: $VST3_PATH"

# ── Run pluginval ──────────────────────────────────────────────

PLUGINVAL_LOG_DIR="$ROOT/bin/pluginval-logs"
mkdir -p "$PLUGINVAL_LOG_DIR"

PLUGINVAL_TIMEOUT="${PLUGINVAL_TIMEOUT:-60000}"
PLUGINVAL_ARGS="--strictness-level $STRICTNESS --verbose --timeout-ms $PLUGINVAL_TIMEOUT --output-dir \"$PLUGINVAL_LOG_DIR\""

if [ -n "$SKIP_GUI" ]; then
    PLUGINVAL_ARGS="$PLUGINVAL_ARGS --skip-gui-tests"
fi

echo "============================================="
echo " Running pluginval (strictness $STRICTNESS)"
echo "============================================="

PLUGINVAL_CMD="\"$PLUGINVAL\" $PLUGINVAL_ARGS --validate \"$VST3_PATH\""

if [ "$OS" = "linux" ]; then
    # Linux CI needs a virtual display for JUCE GUI
    if command -v xvfb-run &> /dev/null; then
        eval xvfb-run -a -s \"-screen 0 1280x720x24\" $PLUGINVAL_CMD
    else
        eval $PLUGINVAL_CMD
    fi
else
    eval $PLUGINVAL_CMD
fi

echo ""
echo "Logs saved to: $PLUGINVAL_LOG_DIR"
ls -la "$PLUGINVAL_LOG_DIR" 2>/dev/null || true

echo "============================================="
echo " pluginval: ALL TESTS PASSED"
echo "============================================="

cd "$ROOT"
