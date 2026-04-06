#!/bin/bash
#
# run_tests.sh — Unified local test runner for osci-render / sosci.
#
# Usage:
#   ./run_tests.sh                                 # unit tests (Debug)
#   ./run_tests.sh --release                       # unit tests (Release)
#   ./run_tests.sh --category LFO                  # specific test category
#   ./run_tests.sh --pluginval                     # pluginval on osci-render
#   ./run_tests.sh --pluginval --plugin sosci       # pluginval on sosci
#   ./run_tests.sh --pluginval --strictness 8       # custom strictness
#   ./run_tests.sh --tsan                           # unit tests under TSan
#   ./run_tests.sh --tsan --pluginval               # pluginval under TSan
#   ./run_tests.sh --tsan --pluginval --tests       # both under TSan
#   ./run_tests.sh --asan --pluginval               # pluginval under ASan
#   ./run_tests.sh --standalone                     # build standalone for manual testing
#   ./run_tests.sh --tsan --standalone              # standalone with TSan
#   ./run_tests.sh --no-build                       # reuse existing builds
#
# Environment:
#   PROJUCER_PATH      Path to Projucer binary (auto-detected if not set)
#   PLUGINVAL_TIMEOUT  Pluginval timeout in ms (default: 120000)
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# ── Defaults ─────────────────────────────────────────────────────────────
SANITIZER=""
PLUGINS=("osci-render")
RUN_PLUGINVAL=false
RUN_TESTS=false
RUN_STANDALONE=false
NO_BUILD=false
CONFIG="Debug"
STRICTNESS=5
TEST_CATEGORY=""
PLUGINVAL_TIMEOUT="${PLUGINVAL_TIMEOUT:-120000}"

# ── Parse args ───────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --tsan)       SANITIZER="tsan"; shift ;;
        --asan)       SANITIZER="asan"; shift ;;
        --plugin)     shift
                      if [[ "$1" == "both" ]]; then
                          PLUGINS=("osci-render" "sosci")
                      else
                          PLUGINS=("$1")
                      fi
                      shift ;;
        --pluginval)  RUN_PLUGINVAL=true; shift ;;
        --tests)      RUN_TESTS=true; shift ;;
        --standalone) RUN_STANDALONE=true; shift ;;
        --no-build)   NO_BUILD=true; shift ;;
        --release)    CONFIG="Release"; shift ;;
        --strictness) STRICTNESS="$2"; shift 2 ;;
        --category)   TEST_CATEGORY="$2"; RUN_TESTS=true; shift 2 ;;
        *)            echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Default mode: unit tests (unless another mode was specified)
if ! $RUN_PLUGINVAL && ! $RUN_TESTS && ! $RUN_STANDALONE; then
    RUN_TESTS=true
fi

# Sanitizer builds always use Debug
if [[ -n "$SANITIZER" ]]; then
    CONFIG="Debug"
fi

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

# ── Detect platform ─────────────────────────────────────────────────────
ARCH="$(uname -m)"
OS_TYPE="$(uname -s)"

if [[ "$OS_TYPE" != "Darwin" && "$OS_TYPE" != "Linux" ]]; then
    echo "Error: Unsupported OS '$OS_TYPE'. Use ci scripts for Windows."
    exit 1
fi

if [[ -n "$SANITIZER" && "$OS_TYPE" != "Darwin" ]]; then
    echo "Error: Sanitizer mode currently supports macOS only."
    exit 1
fi

# ── Sanitizer configuration ─────────────────────────────────────────────
SAN_CFLAGS=""
SAN_LDFLAGS=""
SAN_ENV=""
SAN_LABEL=""
SAN_GREP=""
SUPPRESSIONS="$ROOT/ci/tsan_suppressions.txt"

if [[ "$SANITIZER" == "tsan" ]]; then
    SAN_CFLAGS="-fsanitize=thread -fno-omit-frame-pointer -g"
    SAN_LDFLAGS="-fsanitize=thread -Wl,-no_compact_unwind"
    SAN_LABEL="ThreadSanitizer"
    TSAN_DYLIB=$(find "$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang" \
                      -name 'libclang_rt.tsan_osx_dynamic.dylib' 2>/dev/null | head -1)
    if [[ -z "$TSAN_DYLIB" ]]; then
        echo "Warning: Could not find TSan runtime dylib. DYLD_INSERT_LIBRARIES won't be set."
        SAN_ENV="TSAN_OPTIONS=halt_on_error=0:history_size=4:second_deadlock_stack=1:suppressions=$SUPPRESSIONS"
    else
        SAN_ENV="DYLD_INSERT_LIBRARIES=$TSAN_DYLIB TSAN_OPTIONS=halt_on_error=0:history_size=4:second_deadlock_stack=1:suppressions=$SUPPRESSIONS"
    fi
    SAN_GREP="ThreadSanitizer"
elif [[ "$SANITIZER" == "asan" ]]; then
    SAN_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
    SAN_LDFLAGS="-fsanitize=address -Wl,-no_compact_unwind"
    SAN_LABEL="AddressSanitizer"
    ASAN_DYLIB=$(find "$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang" \
                      -name 'libclang_rt.asan_osx_dynamic.dylib' 2>/dev/null | head -1)
    if [[ -z "$ASAN_DYLIB" ]]; then
        echo "Warning: Could not find ASan runtime dylib. DYLD_INSERT_LIBRARIES won't be set."
        SAN_ENV="ASAN_OPTIONS=halt_on_error=0:detect_leaks=0"
    else
        SAN_ENV="DYLD_INSERT_LIBRARIES=$ASAN_DYLIB ASAN_OPTIONS=halt_on_error=0:detect_leaks=0"
    fi
    SAN_GREP="AddressSanitizer\|UndefinedBehaviorSanitizer"
fi

# ── Log directory (for sanitizer runs) ───────────────────────────────────
LOG_DIR="$ROOT/bin/sanitizer-logs"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
OVERALL_STATUS=0

if [[ -n "$SANITIZER" ]]; then
    mkdir -p "$LOG_DIR"
fi

# ── Print banner ─────────────────────────────────────────────────────────
echo "============================================="
echo " Config:    $CONFIG"
if [[ -n "$SANITIZER" ]]; then
    echo " Sanitizer: $SAN_LABEL"
fi
echo " Plugins:   ${PLUGINS[*]}"
echo " Modes:     $(${RUN_TESTS} && echo "tests ")$(${RUN_PLUGINVAL} && echo "pluginval ")$(${RUN_STANDALONE} && echo "standalone")"
if $RUN_PLUGINVAL; then
    echo " Strictness: $STRICTNESS"
fi
echo "============================================="
echo ""

# ══════════════════════════════════════════════════════════════════════════
# BUILD HELPERS
# ══════════════════════════════════════════════════════════════════════════

build_xcode() {
    local PROJECT="$1"
    local SCHEME="$2"
    local BUILD_CONFIG="$3"

    local LOG_FILE="/dev/null"
    if [[ -n "$SANITIZER" ]]; then
        LOG_FILE="$LOG_DIR/${SANITIZER}-build-${TIMESTAMP}.log"
    fi

    local XCODE_ARGS=(
        -project "$PROJECT"
        -configuration "$BUILD_CONFIG"
        -arch "$ARCH"
    )

    if [[ -n "$SCHEME" ]]; then
        XCODE_ARGS+=(-scheme "$SCHEME")
    fi

    if [[ -n "$SAN_CFLAGS" ]]; then
        XCODE_ARGS+=(
            OTHER_CFLAGS='$(inherited) '"$SAN_CFLAGS"
            OTHER_CPLUSPLUSFLAGS='$(inherited) '"$SAN_CFLAGS"
            OTHER_LDFLAGS='$(inherited) '"$SAN_LDFLAGS"
            GCC_GENERATE_DEBUGGING_SYMBOLS=YES
        )
    fi

    xcodebuild "${XCODE_ARGS[@]}" build 2>&1 | tee "$LOG_FILE" | tail -5
    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        echo "ERROR: xcodebuild failed. See $LOG_FILE"
        exit 1
    fi
}

build_make() {
    local BUILD_DIR="$1"
    local BUILD_CONFIG="$2"

    cd "$BUILD_DIR"
    make -j"$(nproc)" "CONFIG=$BUILD_CONFIG"
    cd "$ROOT"
}

build_plugin() {
    local PLUGIN="$1"
    local TARGET="${2:-VST3}"  # VST3 or Standalone Plugin

    echo "==> Resaving $PLUGIN.jucer..."
    "$PROJUCER" --resave "$ROOT/$PLUGIN.jucer"

    if [[ "$OS_TYPE" == "Darwin" ]]; then
        local BUILD_DIR="$ROOT/Builds/$PLUGIN/MacOSX"
        local SCHEME="$PLUGIN - $TARGET"

        echo "==> Building $PLUGIN ($TARGET, $CONFIG)..."
        cd "$BUILD_DIR"
        build_xcode "$PLUGIN.xcodeproj" "$SCHEME" "$CONFIG"
        cd "$ROOT"
    elif [[ "$OS_TYPE" == "Linux" ]]; then
        echo "==> Building $PLUGIN ($TARGET, $CONFIG)..."
        build_make "$ROOT/Builds/$PLUGIN/LinuxMakefile" "$CONFIG"
    fi
}

build_tests() {
    echo "==> Resaving osci-render-test.jucer..."
    "$PROJUCER" --resave "$ROOT/osci-render-test.jucer"

    if [[ "$OS_TYPE" == "Darwin" ]]; then
        local BUILD_DIR="$ROOT/Builds/Test/MacOSX"

        echo "==> Building tests ($CONFIG)..."
        cd "$BUILD_DIR"
        build_xcode "osci-render-test.xcodeproj" "osci-render-test - ConsoleApp" "$CONFIG"
        cd "$ROOT"
    elif [[ "$OS_TYPE" == "Linux" ]]; then
        echo "==> Building tests ($CONFIG)..."
        build_make "$ROOT/Builds/Test/LinuxMakefile" "$CONFIG"
    fi
}

build_pluginval() {
    local PLUGINVAL_SRC="$ROOT/modules/pluginval"
    local CMAKELISTS="$PLUGINVAL_SRC/CMakeLists.txt"

    echo "==> Building pluginval from source..."

    # When using sanitizers, disable rtcheck — its system-call interceptors
    # conflict with TSan/ASan interceptors.
    local RTCHECK_PATCHED=false
    if [[ -n "$SANITIZER" ]] && grep -q 'set(PLUGINVAL_ENABLE_RTCHECK ON)' "$CMAKELISTS"; then
        sed -i.san_bak 's/set(PLUGINVAL_ENABLE_RTCHECK ON)/# [sanitizer] set(PLUGINVAL_ENABLE_RTCHECK ON)/' "$CMAKELISTS"
        RTCHECK_PATCHED=true
    fi

    cd "$PLUGINVAL_SRC"
    local PLUGINVAL_CONFIG="Release"
    if [[ -n "$SANITIZER" ]]; then
        PLUGINVAL_CONFIG="RelWithDebInfo"
    fi

    cmake -B Builds \
          -DCMAKE_BUILD_TYPE="$PLUGINVAL_CONFIG" \
          -DPLUGINVAL_VST3_VALIDATOR=OFF \
          -Dpluginval_IS_TOP_LEVEL=ON \
          -DWITH_THREAD_SANITIZER=OFF \
          -DWITH_ADDRESS_SANITIZER=OFF \
          .
    cmake --build Builds --config "$PLUGINVAL_CONFIG" --parallel
    cd "$ROOT"

    # Restore the original CMakeLists.txt
    if $RTCHECK_PATCHED && [[ -f "$CMAKELISTS.san_bak" ]]; then
        mv "$CMAKELISTS.san_bak" "$CMAKELISTS"
    fi
}

# ══════════════════════════════════════════════════════════════════════════
# RUN HELPERS
# ══════════════════════════════════════════════════════════════════════════

find_pluginval() {
    local PLUGINVAL_BUILD="$ROOT/modules/pluginval/Builds"
    if [[ "$OS_TYPE" == "Darwin" ]]; then
        # Try RelWithDebInfo first (sanitizer build), then Release
        for CFG in RelWithDebInfo Release; do
            local P="$PLUGINVAL_BUILD/pluginval_artefacts/$CFG/pluginval.app/Contents/MacOS/pluginval"
            if [[ -f "$P" ]]; then
                echo "$P"
                return
            fi
        done
    elif [[ "$OS_TYPE" == "Linux" ]]; then
        for CFG in RelWithDebInfo Release; do
            local P="$PLUGINVAL_BUILD/pluginval_artefacts/$CFG/pluginval"
            if [[ -f "$P" ]]; then
                echo "$P"
                return
            fi
        done
    fi
    echo ""
}

find_vst3() {
    local PLUGIN="$1"
    if [[ "$OS_TYPE" == "Darwin" ]]; then
        echo "$ROOT/Builds/$PLUGIN/MacOSX/build/$CONFIG/$PLUGIN.vst3"
    elif [[ "$OS_TYPE" == "Linux" ]]; then
        echo "$ROOT/Builds/$PLUGIN/LinuxMakefile/build/$PLUGIN.vst3"
    fi
}

run_pluginval() {
    local PLUGIN="$1"
    local VST3_PATH
    VST3_PATH="$(find_vst3 "$PLUGIN")"
    local PLUGINVAL
    PLUGINVAL="$(find_pluginval)"

    if [[ ! -d "$VST3_PATH" ]]; then
        echo "ERROR: VST3 not found at $VST3_PATH"
        echo "Build the plugin first (remove --no-build)."
        return 1
    fi

    if [[ -z "$PLUGINVAL" || ! -f "$PLUGINVAL" ]]; then
        echo "ERROR: pluginval binary not found. Build it first (remove --no-build)."
        return 1
    fi

    echo ""
    echo "============================================="
    echo " Running pluginval on $PLUGIN"
    if [[ -n "$SAN_LABEL" ]]; then
        echo "   Sanitizer:  $SAN_LABEL"
    fi
    echo "   Strictness: $STRICTNESS"
    echo "   VST3:       $VST3_PATH"
    echo "============================================="

    local PLUGINVAL_ARGS=(
        --strictness-level "$STRICTNESS"
        --verbose
        --timeout-ms "$PLUGINVAL_TIMEOUT"
        --validate "$VST3_PATH"
    )

    local EXIT_CODE=0
    if [[ -n "$SAN_ENV" ]]; then
        local LOG_FILE="$LOG_DIR/${SANITIZER}-pluginval-${PLUGIN}-${TIMESTAMP}.log"
        env $SAN_ENV "$PLUGINVAL" "${PLUGINVAL_ARGS[@]}" 2>&1 | tee "$LOG_FILE" || EXIT_CODE=$?
        echo ""
        summarize_log "$LOG_FILE" "$PLUGIN (pluginval)" "$EXIT_CODE"
    else
        if [[ "$OS_TYPE" == "Linux" ]] && command -v xvfb-run &>/dev/null; then
            xvfb-run -a -s "-screen 0 1280x720x24" "$PLUGINVAL" "${PLUGINVAL_ARGS[@]}" || EXIT_CODE=$?
        else
            "$PLUGINVAL" "${PLUGINVAL_ARGS[@]}" || EXIT_CODE=$?
        fi

        if [[ "$EXIT_CODE" -ne 0 ]]; then
            echo ""
            echo "ERROR: pluginval failed with exit code $EXIT_CODE"
            OVERALL_STATUS=1
        else
            echo ""
            echo "============================================="
            echo " pluginval: ALL TESTS PASSED"
            echo "============================================="
        fi
    fi
}

run_unit_tests() {
    local BINARY
    if [[ "$OS_TYPE" == "Darwin" ]]; then
        BINARY="$ROOT/Builds/Test/MacOSX/build/$CONFIG/osci-render-test"
    elif [[ "$OS_TYPE" == "Linux" ]]; then
        BINARY="$ROOT/Builds/Test/LinuxMakefile/build/osci-render-test"
    fi

    if [[ ! -f "$BINARY" ]]; then
        echo "ERROR: Test binary not found at $BINARY"
        return 1
    fi

    echo ""
    echo "============================================="
    echo " Running unit tests"
    if [[ -n "$SAN_LABEL" ]]; then
        echo "   Sanitizer: $SAN_LABEL"
    fi
    echo "============================================="

    local EXIT_CODE=0
    if [[ -n "$SAN_ENV" ]]; then
        local LOG_FILE="$LOG_DIR/${SANITIZER}-tests-${TIMESTAMP}.log"
        if [[ -n "$TEST_CATEGORY" ]]; then
            env $SAN_ENV "$BINARY" "$TEST_CATEGORY" 2>&1 | tee "$LOG_FILE" || EXIT_CODE=$?
        else
            env $SAN_ENV "$BINARY" 2>&1 | tee "$LOG_FILE" || EXIT_CODE=$?
        fi
        echo ""
        summarize_log "$LOG_FILE" "unit tests" "$EXIT_CODE"
    else
        echo "────────────────────────────────────────────────────"
        if [[ -n "$TEST_CATEGORY" ]]; then
            "$BINARY" "$TEST_CATEGORY" || EXIT_CODE=$?
        else
            "$BINARY" || EXIT_CODE=$?
        fi
        echo "────────────────────────────────────────────────────"

        if [[ "$EXIT_CODE" -ne 0 ]]; then
            echo "ERROR: Tests failed with exit code $EXIT_CODE"
            OVERALL_STATUS=1
        else
            echo "==> All tests passed."
        fi
    fi
}

run_standalone() {
    local PLUGIN="$1"
    local BINARY

    if [[ "$OS_TYPE" == "Darwin" ]]; then
        BINARY="$ROOT/Builds/$PLUGIN/MacOSX/build/$CONFIG/$PLUGIN.app/Contents/MacOS/$PLUGIN"
    elif [[ "$OS_TYPE" == "Linux" ]]; then
        BINARY="$ROOT/Builds/$PLUGIN/LinuxMakefile/build/$PLUGIN"
    fi

    if [[ ! -f "$BINARY" ]]; then
        echo "ERROR: Standalone binary not found at $BINARY"
        return 1
    fi

    echo ""
    echo "============================================="
    echo " Launching $PLUGIN standalone"
    if [[ -n "$SAN_LABEL" ]]; then
        echo "   Sanitizer: $SAN_LABEL"
        echo "   (sanitizer output will appear in the terminal)"
    fi
    echo "============================================="

    if [[ -n "$SAN_ENV" ]]; then
        env $SAN_ENV "$BINARY" || true
    else
        "$BINARY" || true
    fi
}

# ── Sanitizer log summary ───────────────────────────────────────────────
summarize_log() {
    local LOG_FILE="$1"
    local CONTEXT="$2"
    local EXIT_CODE="$3"

    local SUMMARIES
    SUMMARIES=$(grep -c "^SUMMARY:" "$LOG_FILE" 2>/dev/null) || true

    if [[ "$SUMMARIES" -gt 0 ]]; then
        echo "─────────────────────────────────────────────"
        echo " WARNING: $SAN_LABEL issues found in $CONTEXT"
        echo "    $SUMMARIES distinct issue(s) detected"
        echo "    Full log: $LOG_FILE"
        echo ""
        echo " Summaries:"
        grep "^SUMMARY:" "$LOG_FILE" 2>/dev/null | sort | uniq -c | sort -rn || true
        echo "─────────────────────────────────────────────"
        OVERALL_STATUS=1
    else
        echo "─────────────────────────────────────────────"
        echo " OK: No $SAN_LABEL issues in $CONTEXT"
        echo "─────────────────────────────────────────────"
    fi

    if [[ "$EXIT_CODE" -ne 0 ]]; then
        echo " Note: process exited with code $EXIT_CODE"
        OVERALL_STATUS=1
    fi
}

# ══════════════════════════════════════════════════════════════════════════
# MAIN
# ══════════════════════════════════════════════════════════════════════════

# ── Build phase ──────────────────────────────────────────────────────────
if ! $NO_BUILD; then
    if $RUN_PLUGINVAL; then
        for PLUGIN in "${PLUGINS[@]}"; do
            build_plugin "$PLUGIN" "VST3"
        done
        build_pluginval
    fi

    if $RUN_STANDALONE; then
        for PLUGIN in "${PLUGINS[@]}"; do
            build_plugin "$PLUGIN" "Standalone Plugin"
        done
    fi

    if $RUN_TESTS; then
        build_tests
    fi
fi

# ── Run phase ────────────────────────────────────────────────────────────
if $RUN_PLUGINVAL; then
    for PLUGIN in "${PLUGINS[@]}"; do
        run_pluginval "$PLUGIN"
    done
fi

if $RUN_TESTS; then
    run_unit_tests
fi

if $RUN_STANDALONE; then
    for PLUGIN in "${PLUGINS[@]}"; do
        run_standalone "$PLUGIN"
    done
fi

# ── Final summary ────────────────────────────────────────────────────────
echo ""
if [[ -n "$SANITIZER" ]]; then
    echo "============================================="
    if [[ "$OVERALL_STATUS" -eq 0 ]]; then
        echo " OK: All $SAN_LABEL checks passed"
    else
        echo " WARNING: $SAN_LABEL issues detected — see logs in $LOG_DIR"
    fi
    echo "   Logs: $LOG_DIR/${SANITIZER}-*-${TIMESTAMP}.log"
    echo "============================================="
fi

exit $OVERALL_STATUS
