#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCEWRIGHT="${JUCEWRIGHT:-}"
JUCEWRIGHT_BUILD_DIR="${JUCEWRIGHT_BUILD_DIR:-/tmp/jucewright-osci-render-cli}"

find_jucewright() {
    if [[ -n "$JUCEWRIGHT" && -x "$JUCEWRIGHT" ]]; then
        return 0
    fi

    local candidate
    for candidate in \
        "$JUCEWRIGHT_BUILD_DIR/jucewright_cli_artefacts/jucewright" \
        "$JUCEWRIGHT_BUILD_DIR/jucewright_cli_artefacts/Debug/jucewright" \
        "$JUCEWRIGHT_BUILD_DIR/jucewright_cli_artefacts/Release/jucewright" \
        "$ROOT_DIR/modules/jucewright/build/jucewright_cli_artefacts/jucewright" \
        "$ROOT_DIR/modules/jucewright/build/jucewright_cli_artefacts/Debug/jucewright" \
        "$ROOT_DIR/modules/jucewright/build/jucewright_cli_artefacts/Release/jucewright"; do
        if [[ -x "$candidate" ]]; then
            JUCEWRIGHT="$candidate"
            return 0
        fi
    done

    candidate="$(find "$JUCEWRIGHT_BUILD_DIR" "$ROOT_DIR/modules/jucewright" -type f -name jucewright -perm -111 2>/dev/null | head -n 1 || true)"
    if [[ -n "$candidate" && -x "$candidate" ]]; then
        JUCEWRIGHT="$candidate"
        return 0
    fi

    return 1
}

build_jucewright() {
    local jobs
    jobs="$(sysctl -n hw.ncpu 2>/dev/null || printf '4')"

    printf '[jucewright-mcp] Building jucewright in %s\n' "$JUCEWRIGHT_BUILD_DIR" >&2
    cmake -S "$ROOT_DIR/modules/jucewright" \
          -B "$JUCEWRIGHT_BUILD_DIR" \
          -DCMAKE_BUILD_TYPE=Debug \
          -DJUCEWRIGHT_BUILD_CLI=ON \
          -DJUCEWRIGHT_BUILD_TESTS=OFF \
          1>&2
    cmake --build "$JUCEWRIGHT_BUILD_DIR" --target jucewright_cli --parallel "$jobs" 1>&2
}

if ! find_jucewright; then
    build_jucewright
    find_jucewright
fi

exec "$JUCEWRIGHT" mcp "$@"
