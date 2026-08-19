#!/bin/bash -e

# pluginval validation script
# Usage (CI):   source ./ci/pluginval.sh "osci-render"
# Usage (CI):   source ./ci/pluginval.sh "osci-render" "osci-render (instrument)"
# Usage (local): ROOT=$(pwd) OS=win ./ci/pluginval.sh "osci-render"
#
# Requires: cmake, a C++ toolchain, and a previously-built plugin.
# Optional second arg: target name (output binary name) if different from plugin name.

PLUGIN="${1:?Usage: pluginval.sh <plugin-name> [target-name]}"
TARGET="${2:-$PLUGIN}"
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
    VST3_PATH="$ROOT/Builds/$PLUGIN/MacOSX/build/Release/$TARGET.vst3"
elif [ "$OS" = "linux" ]; then
    VST3_PATH="$ROOT/Builds/$PLUGIN/LinuxMakefile/build/$TARGET.vst3"
else
    # Try Release first, then Debug
    VST3_PATH="$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Release/VST3/$TARGET.vst3"
    if [ ! -d "$VST3_PATH" ]; then
        VST3_PATH="$ROOT/Builds/$PLUGIN/VisualStudio2022/x64/Debug/VST3/$TARGET.vst3"
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

PLUGINVAL_EXIT=0

if [ "$OS" = "linux" ]; then
    # Print a stack trace on crash (SIGSEGV, SIGABRT, etc.)
    SEGFAULT_LIB=$(find /lib /usr/lib -name 'libSegFault.so' 2>/dev/null | head -1 || true)
    if [ -n "$SEGFAULT_LIB" ]; then
        export LD_PRELOAD="$SEGFAULT_LIB"
        export SEGFAULT_SIGNALS="all"
    fi

    # Keep a wrapper-level stdout/stderr log as well as pluginval's own output
    # files. If pluginval aborts before flushing its output file, this log still
    # gives CI something useful and non-empty to upload.
    LINUX_STDOUT_LOG="$PLUGINVAL_LOG_DIR/pluginval-linux-${TARGET}-stdout.log"

    # Linux CI needs a virtual display for JUCE GUI.
    # NOTE: the backslash-escaped quotes around `-screen 0 1280x720x24` are
    # required so that, after `eval` re-parses the command line, the geometry
    # string is passed as a SINGLE argument to xvfb-run's `-s` flag. Without
    # them xvfb-run sees `-s -screen` and treats `0 1280x720x24` as the
    # command to run (which fails immediately).
    if command -v xvfb-run &> /dev/null; then
        eval xvfb-run -a -s \"-screen 0 1280x720x24\" $PLUGINVAL_CMD 2>&1 | tee "$LINUX_STDOUT_LOG"
        PLUGINVAL_EXIT=${PIPESTATUS[0]}
    else
        eval $PLUGINVAL_CMD 2>&1 | tee "$LINUX_STDOUT_LOG"
        PLUGINVAL_EXIT=${PIPESTATUS[0]}
    fi

    unset LD_PRELOAD SEGFAULT_SIGNALS
elif [ "$OS" = "mac" ]; then
    # Run under lldb in batch mode to capture a stack trace on crash.
    # pluginval intercepts signals itself, so macOS doesn't generate .ips crash reports.
    # lldb catches the signal first and prints a full backtrace.
    LLDB_CRASH_LOG="$PLUGINVAL_LOG_DIR/pluginval_crash_bt.log"
    if command -v lldb &> /dev/null; then
        # Write an lldb command script:
        # - Configure signal handling to stop on crash signals
        # - Run the process
        # - After stop/exit: dump backtraces (harmless if process exited normally)
        # - Exit with code 1 on crash, 0 on normal exit
        LLDB_SCRIPT="$PLUGINVAL_LOG_DIR/lldb_commands.txt"
        cat > "$LLDB_SCRIPT" << 'LLDB_EOF'
process handle SIGSEGV --stop true --pass false --notify true
process handle SIGABRT --stop true --pass false --notify true
process handle SIGBUS  --stop true --pass false --notify true
run
script import lldb; proc = lldb.debugger.GetSelectedTarget().GetProcess(); crashed = proc.GetState() != lldb.eStateExited; lldb.debugger.HandleCommand("thread backtrace all" if crashed else "script pass"); lldb.debugger.HandleCommand("quit 1" if crashed else "quit " + str(proc.GetExitStatus()))
LLDB_EOF

        lldb --batch \
            -s "$LLDB_SCRIPT" \
            -- "$PLUGINVAL" --strictness-level $STRICTNESS --verbose \
               --timeout-ms $PLUGINVAL_TIMEOUT \
               --output-dir "$PLUGINVAL_LOG_DIR" \
               ${SKIP_GUI:+--skip-gui-tests} \
               --validate "$VST3_PATH" \
            2>&1 | tee "$LLDB_CRASH_LOG"
        PLUGINVAL_EXIT=${PIPESTATUS[0]}

        # If pluginval exited non-zero, highlight the stack trace
        if [ "$PLUGINVAL_EXIT" -ne 0 ]; then
            echo ""
            echo "============================================="
            echo " pluginval crashed (exit code $PLUGINVAL_EXIT)"
            echo " Stack trace summary:"
            echo "============================================="
            grep -E 'frame #|Thread |stop reason|signal' "$LLDB_CRASH_LOG" | head -100 || true
        fi
    else
        eval $PLUGINVAL_CMD || PLUGINVAL_EXIT=$?
    fi
else
    # Windows: use procdump to capture a minidump on crash (SIGSEGV, unhandled exception).
    # procdump must be on PATH (installed in the CI workflow).
    PROCDUMP_DIR="$PLUGINVAL_LOG_DIR/crashdumps"
    mkdir -p "$PROCDUMP_DIR"

    if command -v procdump &> /dev/null; then
        echo "Running pluginval under procdump (crash dump dir: $PROCDUMP_DIR)"
        # -e 1 = write dump on first-chance unhandled exception
        # -ma  = full memory dump (needed for useful stack analysis)
        # -accepteula = suppress EULA prompt in CI
        # -x   = launch process mode: -x <dump_folder> <application> [args]
        #
        # procdump's exit code in -x mode is UNRELIABLE: it returns non-zero
        # when "dump count not reached" (i.e. no crash occurred). We ignore
        # procdump's exit code entirely and instead check the pluginval log
        # file for the SUCCESS marker.
        procdump -e 1 -ma -accepteula \
            -x "$PROCDUMP_DIR" \
            "$PLUGINVAL" --strictness-level $STRICTNESS --verbose \
                --timeout-ms $PLUGINVAL_TIMEOUT \
                --output-dir "$PLUGINVAL_LOG_DIR" \
                ${SKIP_GUI:+--skip-gui-tests} \
                --validate "$VST3_PATH" \
            || true

        # Check the pluginval log for SUCCESS instead of trusting procdump's exit code
        if ! grep -rq '^SUCCESS$' "$PLUGINVAL_LOG_DIR"/ 2>/dev/null; then
            PLUGINVAL_EXIT=1
        fi

        # Report any generated dump files
        DUMP_FILES=($(find "$PROCDUMP_DIR" -name '*.dmp' 2>/dev/null))
        if [ ${#DUMP_FILES[@]} -gt 0 ]; then
            echo ""
            echo "============================================="
            echo " procdump captured ${#DUMP_FILES[@]} crash dump(s):"
            echo "============================================="
            for d in "${DUMP_FILES[@]}"; do
                echo "  $(basename "$d") ($(du -h "$d" | cut -f1))"
            done

            # Try to extract a basic stack trace using cdb if available
            if command -v cdb &> /dev/null; then
                CDB_LOG="$PLUGINVAL_LOG_DIR/pluginval_crash_bt.log"
                for d in "${DUMP_FILES[@]}"; do
                    echo ""
                    echo "--- Stack trace from $(basename "$d") ---"
                    cdb -z "$d" -c "!analyze -v; ~*k; q" 2>&1 | tee -a "$CDB_LOG" | tail -80
                done
            else
                echo "(cdb not found — install Windows SDK Debugging Tools to get inline stack traces)"
                echo "Dump files will be uploaded to MEGA for offline analysis with WinDbg."
            fi
        fi
    else
        echo "WARNING: procdump not found, running pluginval without crash dump capture"
        eval $PLUGINVAL_CMD || PLUGINVAL_EXIT=$?
    fi
fi

echo ""
echo "Logs saved to: $PLUGINVAL_LOG_DIR"
ls -la "$PLUGINVAL_LOG_DIR" 2>/dev/null || true

if [ "$PLUGINVAL_EXIT" -ne 0 ]; then
    echo ""
    echo "============================================="
    echo " pluginval: FAILED (exit code $PLUGINVAL_EXIT)"
    echo "============================================="
    exit "$PLUGINVAL_EXIT"
fi

echo "============================================="
echo " pluginval: ALL TESTS PASSED"
echo "============================================="

cd "$ROOT"
