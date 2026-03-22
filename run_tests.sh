#!/bin/bash
#
# run_tests.sh — Build and run osci-render tests locally (macOS / Linux).
# Usage: ./run_tests.sh [--release] [--category <name>]
#
# Options:
#   --release       Build in Release mode (default: Debug)
#   --category LFO  Only run tests in the given JUCE UnitTest category
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

CONFIG="Debug"
CATEGORY=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release) CONFIG="Release"; shift ;;
        --category) CATEGORY="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Locate Projucer ──────────────────────────────────────────────────────
if [[ -n "${PROJUCER_PATH:-}" ]]; then
    PROJUCER="$PROJUCER_PATH"
elif [[ -x "$HOME/JUCE/Projucer.app/Contents/MacOS/Projucer" ]]; then
    PROJUCER="$HOME/JUCE/Projucer.app/Contents/MacOS/Projucer"
elif command -v Projucer &>/dev/null; then
    PROJUCER="Projucer"
else
    echo "Error: Cannot find Projucer. Set PROJUCER_PATH or install JUCE."
    exit 1
fi

echo "==> Resaving test project…"
"$PROJUCER" --resave osci-render-test.jucer

# ── Build ────────────────────────────────────────────────────────────────
OS_TYPE="$(uname -s)"
if [[ "$OS_TYPE" == "Darwin" ]]; then
    ARCH="$(uname -m)"
    echo "==> Building ($CONFIG, $ARCH)…"
    cd Builds/Test/MacOSX
    xcodebuild -project osci-render-test.xcodeproj \
               -configuration "$CONFIG" \
               -arch "$ARCH" \
               build 2>&1 | tail -5
    BINARY="build/$CONFIG/osci-render-test"
elif [[ "$OS_TYPE" == "Linux" ]]; then
    echo "==> Building ($CONFIG)…"
    cd Builds/Test/LinuxMakefile
    make -j"$(nproc)" "CONFIG=$CONFIG"
    BINARY="build/osci-render-test"
else
    echo "Error: Unsupported OS '$OS_TYPE'. Use ci/test.sh for Windows."
    exit 1
fi

# ── Run ──────────────────────────────────────────────────────────────────
echo ""
echo "==> Running tests…"
echo "────────────────────────────────────────────────────"

if [[ -n "$CATEGORY" ]]; then
    "$BINARY" "$CATEGORY"
else
    "$BINARY"
fi

echo "────────────────────────────────────────────────────"
echo "==> All tests passed."
