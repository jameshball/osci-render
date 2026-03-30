#!/bin/bash
#
# run_sanitizers.sh — Build and test osci-render / sosci under ASan or TSan.
#
# Usage:
#   ./run_sanitizers.sh --tsan                             # TSan + pluginval on osci-render
#   ./run_sanitizers.sh --asan                             # ASan + pluginval on osci-render
#   ./run_sanitizers.sh --tsan --plugin sosci               # TSan + pluginval on sosci
#   ./run_sanitizers.sh --tsan --plugin both                # TSan + pluginval on both
#   ./run_sanitizers.sh --tsan --tests                      # TSan on unit tests only
#   ./run_sanitizers.sh --asan --pluginval --tests          # ASan on pluginval AND unit tests
#   ./run_sanitizers.sh --tsan --standalone                 # Build standalone with TSan for manual testing
#   ./run_sanitizers.sh --tsan --strictness 8               # Pluginval strictness (default: 5)
#   ./run_sanitizers.sh --tsan --no-build                   # Reuse existing sanitizer builds
#
# Environment:
#   PROJUCER_PATH   Path to Projucer binary (auto-detected if not set)
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
        --strictness) STRICTNESS="$2"; shift 2 ;;
        --category)   TEST_CATEGORY="$2"; RUN_TESTS=true; shift 2 ;;
        *)            echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ -z "$SANITIZER" ]]; then
    echo "Error: must specify --tsan or --asan"
    echo "Usage: $0 --tsan|--asan [--plugin NAME] [--pluginval] [--tests] [--standalone]"
    exit 1
fi

# Default to pluginval if no mode selected
if ! $RUN_PLUGINVAL && ! $RUN_TESTS && ! $RUN_STANDALONE; then
    RUN_PLUGINVAL=true
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

# ── Platform ─────────────────────────────────────────────────────────────
ARCH="$(uname -m)"
OS_TYPE="$(uname -s)"
if [[ "$OS_TYPE" != "Darwin" ]]; then
    echo "Error: This script currently supports macOS only."
    echo "For Linux, the approach is similar but uses make instead of xcodebuild."
    exit 1
fi

# ── Sanitizer flags ─────────────────────────────────────────────────────
SUPPRESSIONS="$ROOT/ci/tsan_suppressions.txt"

if [[ "$SANITIZER" == "tsan" ]]; then
    SAN_CFLAGS="-fsanitize=thread -fno-omit-frame-pointer -g"
    SAN_LDFLAGS="-fsanitize=thread -Wl,-no_compact_unwind"
    CMAKE_SAN_FLAG="-DWITH_THREAD_SANITIZER=ON"
    SAN_LABEL="ThreadSanitizer"
    SAN_ENV="TSAN_OPTIONS=halt_on_error=0:history_size=4:second_deadlock_stack=1:suppressions=$SUPPRESSIONS"
    SAN_GREP="ThreadSanitizer"
elif [[ "$SANITIZER" == "asan" ]]; then
    SAN_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
    SAN_LDFLAGS="-fsanitize=address -Wl,-no_compact_unwind"
    CMAKE_SAN_FLAG="-DWITH_ADDRESS_SANITIZER=ON"
    SAN_LABEL="AddressSanitizer"
    # ASan runtime must be pre-loaded for plugins loaded via dlopen (VST3).
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

# ── Log directory ────────────────────────────────────────────────────────
LOG_DIR="$ROOT/bin/sanitizer-logs"
mkdir -p "$LOG_DIR"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"

OVERALL_STATUS=0

echo "============================================="
echo " Sanitizer: $SAN_LABEL"
echo " Plugins:   ${PLUGINS[*]}"
echo " Modes:     $(${RUN_PLUGINVAL} && echo "pluginval ")$(${RUN_TESTS} && echo "tests ")$(${RUN_STANDALONE} && echo "standalone")"
echo " Logs:      $LOG_DIR"
echo "============================================="
echo ""

# ── Helper: build a plugin with sanitizer flags ─────────────────────────
build_plugin() {
    local PLUGIN="$1"
    local TARGET="${2:-VST3}"  # VST3 or Standalone Plugin

    echo "==> Resaving $PLUGIN.jucer..."
    "$PROJUCER" --resave "$ROOT/$PLUGIN.jucer"

    local BUILD_DIR="$ROOT/Builds/$PLUGIN/MacOSX"
    local SCHEME="$PLUGIN - $TARGET"

    echo "==> Building $PLUGIN ($TARGET) with $SAN_LABEL..."
    cd "$BUILD_DIR"
    local BUILD_LOG="$LOG_DIR/${SANITIZER}-build-${PLUGIN}-${TIMESTAMP}.log"
    xcodebuild -project "$PLUGIN.xcodeproj" \
               -scheme "$SCHEME" \
               -configuration Debug \
               -arch "$ARCH" \
               OTHER_CFLAGS='$(inherited) '"$SAN_CFLAGS" \
               OTHER_CPLUSPLUSFLAGS='$(inherited) '"$SAN_CFLAGS" \
               OTHER_LDFLAGS='$(inherited) '"$SAN_LDFLAGS" \
               GCC_GENERATE_DEBUGGING_SYMBOLS=YES \
               build 2>&1 | tee "$BUILD_LOG" | tail -20
    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        echo "ERROR: xcodebuild failed for $PLUGIN ($TARGET). See $BUILD_LOG"
        exit 1
    fi
    cd "$ROOT"
}

# ── Helper: build unit tests with sanitizer flags ───────────────────────
build_tests() {
    echo "==> Resaving osci-render-test.jucer..."
    "$PROJUCER" --resave "$ROOT/osci-render-test.jucer"

    local BUILD_DIR="$ROOT/Builds/Test/MacOSX"

    echo "==> Building tests with $SAN_LABEL..."
    cd "$BUILD_DIR"
    local BUILD_LOG="$LOG_DIR/${SANITIZER}-build-tests-${TIMESTAMP}.log"
    xcodebuild -project osci-render-test.xcodeproj \
               -configuration Debug \
               -arch "$ARCH" \
               OTHER_CFLAGS='$(inherited) '"$SAN_CFLAGS" \
               OTHER_CPLUSPLUSFLAGS='$(inherited) '"$SAN_CFLAGS" \
               OTHER_LDFLAGS='$(inherited) '"$SAN_LDFLAGS" \
               GCC_GENERATE_DEBUGGING_SYMBOLS=YES \
               build 2>&1 | tee "$BUILD_LOG" | tail -20
    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        echo "ERROR: xcodebuild failed for tests. See $BUILD_LOG"
        exit 1
    fi
    cd "$ROOT"
}

# ── Helper: build pluginval with sanitizer flags ────────────────────────
build_pluginval() {
    local PLUGINVAL_SRC="$ROOT/modules/pluginval"
    local CMAKELISTS="$PLUGINVAL_SRC/CMakeLists.txt"

    echo "==> Building pluginval (without sanitizer instrumentation)..."
    echo "    (pluginval is the test harness; only the plugin VST3 needs sanitizer flags)"
    echo "    (rtcheck disabled — its interceptors conflict with sanitizer runtimes)"

    # Temporarily disable rtcheck: its system-call interceptors (stat, malloc, etc.)
    # conflict with TSan/ASan interceptors, causing a crash during sanitizer init.
    # pluginval's CMakeLists.txt uses set() (not option/cache), so we must patch it.
    local RTCHECK_PATCHED=false
    if grep -q 'set(PLUGINVAL_ENABLE_RTCHECK ON)' "$CMAKELISTS"; then
        sed -i.san_bak 's/set(PLUGINVAL_ENABLE_RTCHECK ON)/# [sanitizer] set(PLUGINVAL_ENABLE_RTCHECK ON)/' "$CMAKELISTS"
        RTCHECK_PATCHED=true
    fi

    cd "$PLUGINVAL_SRC"
    # Don't instrument pluginval itself — Apple Clang can crash (segfault) when
    # compiling large JUCE ObjC++ files with sanitizer flags.  The sanitizer
    # runtime is injected at launch via DYLD_INSERT_LIBRARIES, so it still
    # monitors the instrumented plugin code loaded by pluginval.
    cmake -B Builds \
          -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DPLUGINVAL_VST3_VALIDATOR=OFF \
          -Dpluginval_IS_TOP_LEVEL=ON \
          -DWITH_THREAD_SANITIZER=OFF \
          -DWITH_ADDRESS_SANITIZER=OFF \
          .
    cmake --build Builds --config RelWithDebInfo --parallel
    cd "$ROOT"

    # Restore the original CMakeLists.txt
    if $RTCHECK_PATCHED && [[ -f "$CMAKELISTS.san_bak" ]]; then
        mv "$CMAKELISTS.san_bak" "$CMAKELISTS"
    fi
}

# ── Helper: run pluginval and capture sanitizer output ──────────────────
run_pluginval() {
    local PLUGIN="$1"
    local VST3_PATH="$ROOT/Builds/$PLUGIN/MacOSX/build/Debug/$PLUGIN.vst3"
    local PLUGINVAL="$ROOT/modules/pluginval/Builds/pluginval_artefacts/RelWithDebInfo/pluginval.app/Contents/MacOS/pluginval"

    if [[ ! -d "$VST3_PATH" ]]; then
        echo "ERROR: VST3 not found at $VST3_PATH"
        echo "Build the plugin first (remove --no-build)."
        return 1
    fi

    if [[ ! -f "$PLUGINVAL" ]]; then
        echo "ERROR: pluginval binary not found at $PLUGINVAL"
        return 1
    fi

    local LOG_FILE="$LOG_DIR/${SANITIZER}-pluginval-${PLUGIN}-${TIMESTAMP}.log"

    echo ""
    echo "============================================="
    echo " Running pluginval on $PLUGIN"
    echo "   Sanitizer:  $SAN_LABEL"
    echo "   Strictness: $STRICTNESS"
    echo "   VST3:       $VST3_PATH"
    echo "   Log:        $LOG_FILE"
    echo "============================================="

    local EXIT_CODE=0
    env $SAN_ENV \
        "$PLUGINVAL" \
        --strictness-level "$STRICTNESS" \
        --verbose \
        --timeout-ms "$PLUGINVAL_TIMEOUT" \
        --validate "$VST3_PATH" \
        2>&1 | tee "$LOG_FILE" || EXIT_CODE=$?

    echo ""
    summarize_log "$LOG_FILE" "$PLUGIN (pluginval)" "$EXIT_CODE"
}

# ── Helper: run unit tests and capture sanitizer output ─────────────────
run_tests() {
    local BINARY="$ROOT/Builds/Test/MacOSX/build/Debug/osci-render-test"
    local LOG_FILE="$LOG_DIR/${SANITIZER}-tests-${TIMESTAMP}.log"

    if [[ ! -f "$BINARY" ]]; then
        echo "ERROR: Test binary not found at $BINARY"
        return 1
    fi

    echo ""
    echo "============================================="
    echo " Running unit tests with $SAN_LABEL"
    echo "   Log: $LOG_FILE"
    echo "============================================="

    local EXIT_CODE=0
    if [[ -n "$TEST_CATEGORY" ]]; then
        env $SAN_ENV "$BINARY" "$TEST_CATEGORY" 2>&1 | tee "$LOG_FILE" || EXIT_CODE=$?
    else
        env $SAN_ENV "$BINARY" 2>&1 | tee "$LOG_FILE" || EXIT_CODE=$?
    fi

    echo ""
    summarize_log "$LOG_FILE" "unit tests" "$EXIT_CODE"
}

# ── Helper: summarize sanitizer findings ────────────────────────────────
summarize_log() {
    local LOG_FILE="$1"
    local CONTEXT="$2"
    local EXIT_CODE="$3"

    local FINDINGS
    FINDINGS=$(grep -c "$SAN_GREP" "$LOG_FILE" 2>/dev/null) || true

    # Count unique warnings (each WARNING/ERROR block)
    local UNIQUE_WARNINGS
    UNIQUE_WARNINGS=$(grep -c "^WARNING:" "$LOG_FILE" 2>/dev/null) || true

    # Count SUMMARY lines (each one = one distinct issue)
    local SUMMARIES
    SUMMARIES=$(grep -c "^SUMMARY:" "$LOG_FILE" 2>/dev/null) || true

    if [[ "$FINDINGS" -gt 0 ]] || [[ "$SUMMARIES" -gt 0 ]]; then
        echo "─────────────────────────────────────────────"
        echo " ⚠  $SAN_LABEL issues found in $CONTEXT"
        echo "    $SUMMARIES distinct issue(s) detected"
        echo "    Full log: $LOG_FILE"
        echo ""
        echo " Summaries:"
        grep "^SUMMARY:" "$LOG_FILE" 2>/dev/null | sort | uniq -c | sort -rn || true
        echo "─────────────────────────────────────────────"
        OVERALL_STATUS=1
    else
        echo "─────────────────────────────────────────────"
        echo " ✓  No $SAN_LABEL issues in $CONTEXT"
        echo "─────────────────────────────────────────────"
    fi

    if [[ "$EXIT_CODE" -ne 0 ]]; then
        echo " Note: process exited with code $EXIT_CODE"
        echo " (This may include pluginval test failures separate from sanitizer issues)"
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
    run_tests
fi

if $RUN_STANDALONE; then
    for PLUGIN in "${PLUGINS[@]}"; do
        BINARY="$ROOT/Builds/$PLUGIN/MacOSX/build/Debug/$PLUGIN.app"
        echo ""
        echo "============================================="
        echo " Launching $PLUGIN standalone with $SAN_LABEL"
        echo " (sanitizer output will appear in the terminal)"
        echo "============================================="
        echo ""
        echo "Run manually with:"
        echo "  $SAN_ENV $BINARY/Contents/MacOS/$PLUGIN"
        echo ""
        # Launch in foreground so sanitizer output is visible
        env $SAN_ENV "$BINARY/Contents/MacOS/$PLUGIN" || true
    done
fi

# ── Final summary ────────────────────────────────────────────────────────
echo ""
echo "============================================="
if [[ "$OVERALL_STATUS" -eq 0 ]]; then
    echo " ✓  All $SAN_LABEL checks passed"
else
    echo " ⚠  $SAN_LABEL issues detected — see logs in $LOG_DIR"
fi
echo "   Logs: $LOG_DIR/${SANITIZER}-*-${TIMESTAMP}.log"
echo "============================================="

exit $OVERALL_STATUS
