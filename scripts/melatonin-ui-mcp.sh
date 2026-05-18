#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MELATONIN_UI="${MELATONIN_UI:-}"
MELATONIN_UI_BUILD_DIR="${MELATONIN_UI_BUILD_DIR:-/tmp/melatonin-inspector-osci-render-cli}"

find_melatonin_ui() {
    if [[ -n "$MELATONIN_UI" && -x "$MELATONIN_UI" ]]; then
        return 0
    fi

    local candidate
    for candidate in \
        "$MELATONIN_UI_BUILD_DIR/melatonin-ui_artefacts/Debug/melatonin-ui" \
        "$MELATONIN_UI_BUILD_DIR/melatonin-ui_artefacts/Release/melatonin-ui" \
        "$ROOT_DIR/modules/melatonin_inspector/build/melatonin-ui_artefacts/Debug/melatonin-ui" \
        "$ROOT_DIR/modules/melatonin_inspector/build/melatonin-ui_artefacts/Release/melatonin-ui"; do
        if [[ -x "$candidate" ]]; then
            MELATONIN_UI="$candidate"
            return 0
        fi
    done

    candidate="$(find "$MELATONIN_UI_BUILD_DIR" "$ROOT_DIR/modules/melatonin_inspector" -type f -name melatonin-ui -perm -111 2>/dev/null | head -n 1 || true)"
    if [[ -n "$candidate" && -x "$candidate" ]]; then
        MELATONIN_UI="$candidate"
        return 0
    fi

    return 1
}

build_melatonin_ui() {
    local jobs
    jobs="$(sysctl -n hw.ncpu 2>/dev/null || printf '4')"

    printf '[melatonin-ui-mcp] Building melatonin-ui in %s\n' "$MELATONIN_UI_BUILD_DIR" >&2
    cmake -S "$ROOT_DIR/modules/melatonin_inspector" \
          -B "$MELATONIN_UI_BUILD_DIR" \
          -DCMAKE_BUILD_TYPE=Debug \
          -DMELATONIN_INSPECTOR_ENABLE_AUTOMATION=ON \
          -DMELATONIN_INSPECTOR_BUILD_CLI=ON \
          1>&2
    cmake --build "$MELATONIN_UI_BUILD_DIR" --target melatonin-ui --parallel "$jobs" 1>&2
}

if ! find_melatonin_ui; then
    build_melatonin_ui
    find_melatonin_ui
fi

exec "$MELATONIN_UI" mcp "$@"
