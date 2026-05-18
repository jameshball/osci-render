#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SESSION="${SESSION:-osci-render}"
AUTOMATION_ARTIFACT_ROOT="${AUTOMATION_ARTIFACT_ROOT:-$HOME/Library/Caches/osci-render/osci-render-jucewright-automation}"
ARTIFACT_DIR="${ARTIFACT_DIR:-$AUTOMATION_ARTIFACT_ROOT/browse/$(date +%Y%m%d-%H%M%S)}"
RECORDING_DIR_PROVIDED="${RECORDING_DIR+x}"
RECORDING_DIR="${RECORDING_DIR:-$ARTIFACT_DIR/recordings}"
AUTOMATION_HOME_ROOT_PROVIDED="${AUTOMATION_HOME_ROOT+x}"
AUTOMATION_HOME_ROOT="${AUTOMATION_HOME_ROOT:-$ARTIFACT_DIR/home}"
SOURCE_HOME="${SOURCE_HOME:-$HOME}"
APP_BUNDLE="${APP_BUNDLE:-$ROOT_DIR/Builds/osci-render/MacOSX/build/Debug/osci-render.app}"
APP_EXECUTABLE="${APP_EXECUTABLE:-$APP_BUNDLE/Contents/MacOS/osci-render}"
JUCEWRIGHT="${JUCEWRIGHT:-}"
JUCEWRIGHT_BUILD_DIR="${JUCEWRIGHT_BUILD_DIR:-/tmp/jucewright-osci-render-cli}"
SESSION_TIMEOUT_SECONDS="${SESSION_TIMEOUT_SECONDS:-120}"

BUILD_APP=0
QUICK=0
KEEP_APP=0
INCLUDE_NATIVE=0
APP_PID=""
STEP=0
LAUNCH_INDEX=0
FAILURES=()
OPTIONAL_FAILURES=()

usage() {
    cat <<USAGE
Usage: $(basename "$0") [options]

Options:
  --build-app       Resave the Projucer project and build the Debug standalone app.
  --quick           Run a shorter smoke pass instead of the exhaustive pass.
  --keep-app        Leave the launched osci-render process running.
  --native-dialogs  Include actions that may open native file dialogs.
  --artifact-dir D  Write logs, JSON snapshots, screenshots, and traces to D.
  --jucewright P    Use a specific jucewright executable.
  --help            Show this help.

Environment:
  SESSION, SESSION_TIMEOUT_SECONDS, ARTIFACT_DIR, RECORDING_DIR, AUTOMATION_ARTIFACT_ROOT, AUTOMATION_HOME_ROOT, APP_BUNDLE, APP_EXECUTABLE, JUCEWRIGHT, JUCEWRIGHT_BUILD_DIR
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-app) BUILD_APP=1; shift ;;
        --quick) QUICK=1; shift ;;
        --keep-app) KEEP_APP=1; shift ;;
        --native-dialogs) INCLUDE_NATIVE=1; shift ;;
        --artifact-dir) ARTIFACT_DIR="$2"; shift 2 ;;
        --jucewright) JUCEWRIGHT="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$AUTOMATION_HOME_ROOT_PROVIDED" ]]; then
    AUTOMATION_HOME_ROOT="$ARTIFACT_DIR/home"
fi

if [[ -z "$RECORDING_DIR_PROVIDED" ]]; then
    RECORDING_DIR="$ARTIFACT_DIR/recordings"
fi

mkdir -p "$ARTIFACT_DIR" "$RECORDING_DIR" "$AUTOMATION_HOME_ROOT"

log() {
    printf '[browse-osci-render] %s\n' "$*"
}

slug() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr -cs '[:alnum:]' '_' | sed 's/^_//;s/_$//'
}

run_step() {
    local label="$1"
    shift
    STEP=$((STEP + 1))
    local file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_$(slug "$label").log"

    log "RUN $label"
    if "$@" >"$file" 2>&1; then
        log "OK  $label -> $file"
        return 0
    else
        local status=$?
        log "FAIL $label -> $file"
        FAILURES+=("$label (exit $status): $file")
        return "$status"
    fi
}

try_step() {
    local label="$1"
    shift
    STEP=$((STEP + 1))
    local file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_optional_$(slug "$label").log"

    log "TRY $label"
    if "$@" >"$file" 2>&1; then
        log "OK  $label -> $file"
        return 0
    else
        local status=$?
        log "SKIP/FAIL optional $label -> $file"
        OPTIONAL_FAILURES+=("$label (exit $status): $file")
        return 0
    fi
}

probe_step() {
    local label="$1"
    shift
    STEP=$((STEP + 1))
    local file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_probe_$(slug "$label").log"

    log "PROBE $label"
    if "$@" >"$file" 2>&1; then
        log "OK  probe $label -> $file"
        return 0
    else
        log "INFO probe unavailable $label -> $file"
        return 0
    fi
}

native_escape() {
    osascript -e 'tell application "System Events" to key code 53'
}

try_open_effect_browser() {
    local effect="$1"
    STEP=$((STEP + 1))
    local file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_ensure_effect_browser_for_$(slug "$effect").log"

    log "TRY open effect browser for $effect"
    if cli click --name "Add new effect" --exact --timeout-ms 3000 >"$file" 2>&1; then
        log "OK  open effect browser for $effect -> $file"
    else
        log "INFO effect browser for $effect was already open or will be verified by the next required step -> $file"
    fi
}

example_nth() {
    case "$1" in
        "Hello World") echo 0 ;;
        "Greek") echo 1 ;;
        "osci-render") echo 2 ;;
        "Paperclip") echo 3 ;;
        "sosci") echo 4 ;;
        "Spiral") echo 5 ;;
        "Shape Generator") echo 6 ;;
        "Squiggles") echo 7 ;;
        "Donut") echo 8 ;;
        "Graph") echo 9 ;;
        "Gravity Well") echo 10 ;;
        "Helix") echo 11 ;;
        "Human") echo 12 ;;
        "Hypercube") echo 13 ;;
        "Mushroom") echo 14 ;;
        "Planet") echo 15 ;;
        "Cube") echo 16 ;;
        "Diamond") echo 17 ;;
        "Dodecahedron") echo 18 ;;
        "Humanoid Quad") echo 19 ;;
        "Icosahedron") echo 20 ;;
        "Lamp") echo 21 ;;
        "Shuttle") echo 22 ;;
        "Suzanne") echo 23 ;;
        "Teapot") echo 24 ;;
        "Tetrahedron") echo 25 ;;
        "Air Horn") echo 26 ;;
        "Alien") echo 27 ;;
        "Bicycle") echo 28 ;;
        "Card") echo 29 ;;
        "Cash") echo 30 ;;
        "Cat") echo 31 ;;
        "Clippy") echo 32 ;;
        "Desktop") echo 33 ;;
        "Puzzle") echo 34 ;;
        "Skull") echo 35 ;;
        "Snowflake") echo 36 ;;
        "Yin Yang") echo 37 ;;
        "Koch Snowflake") echo 38 ;;
        "Sierpinski Triangle") echo 39 ;;
        "Dragon Curve") echo 40 ;;
        "Binary Tree") echo 41 ;;
        "Hilbert Curve") echo 42 ;;
        *) return 1 ;;
    esac
}

effect_nth() {
    case "$1" in
        "Bit Crush") echo 0 ;;
        "Bounce") echo 1 ;;
        "Bulge") echo 2 ;;
        "Dash") echo 3 ;;
        "Delay") echo 4 ;;
        "Distort") echo 5 ;;
        "Duplicator") echo 6 ;;
        "God Ray") echo 7 ;;
        "Kaleidoscope") echo 8 ;;
        "Lua Effect") echo 9 ;;
        "Multiplex") echo 10 ;;
        "Polygonizer") echo 11 ;;
        "Ripple") echo 12 ;;
        "Rotate") echo 13 ;;
        "Scale") echo 14 ;;
        "Skew") echo 15 ;;
        "Smoothing") echo 16 ;;
        "Spiral Bit Crush") echo 17 ;;
        "Swirl") echo 18 ;;
        "Trace") echo 19 ;;
        "Translate") echo 20 ;;
        "Twist") echo 21 ;;
        "Unfold") echo 22 ;;
        "Vector Cancelling") echo 23 ;;
        "Vortex") echo 24 ;;
        "Wobble") echo 25 ;;
        *) return 1 ;;
    esac
}

mod_handle_nth() {
    case "$1" in
        "ENV 1") echo 0 ;;
        "ENV 2") echo 1 ;;
        "ENV 3") echo 2 ;;
        "ENV 4") echo 3 ;;
        "ENV 5") echo 4 ;;
        "LFO 1") echo 5 ;;
        "LFO 2") echo 6 ;;
        "LFO 3") echo 7 ;;
        "LFO 4") echo 8 ;;
        "LFO 5") echo 9 ;;
        "LFO 6") echo 10 ;;
        "LFO 7") echo 11 ;;
        "LFO 8") echo 12 ;;
        "RAND 1") echo 13 ;;
        "RAND 2") echo 14 ;;
        "RAND 3") echo 15 ;;
        "INPUT") echo 16 ;;
        *) return 1 ;;
    esac
}

die() {
    echo "$*" >&2
    exit 1
}

stop_app() {
    if [[ -n "$APP_PID" && "$KEEP_APP" -eq 0 ]]; then
        kill "$APP_PID" >/dev/null 2>&1 || true
        wait "$APP_PID" >/dev/null 2>&1 || true
    fi
    if [[ "$KEEP_APP" -eq 0 ]]; then
        pkill -x "osci-render" >/dev/null 2>&1 || true
    fi
    APP_PID=""
}

cleanup() {
    stop_app
}
trap cleanup EXIT

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

    log "Building jucewright in $JUCEWRIGHT_BUILD_DIR"
    cmake -S "$ROOT_DIR/modules/jucewright" \
          -B "$JUCEWRIGHT_BUILD_DIR" \
          -DCMAKE_BUILD_TYPE=Debug \
          -DJUCEWRIGHT_BUILD_CLI=ON \
          -DJUCEWRIGHT_BUILD_TESTS=OFF
    cmake --build "$JUCEWRIGHT_BUILD_DIR" --target jucewright_cli --parallel "$jobs"
}

build_app() {
    log "Building osci-render Debug standalone"
    (
        cd "$ROOT_DIR" \
            && ~/JUCE/Projucer.app/Contents/MacOS/Projucer --resave osci-render.jucer \
            && cd Builds/osci-render/MacOSX \
            && xcodebuild -project osci-render.xcodeproj \
                -scheme "osci-render - Standalone Plugin" \
                -configuration Debug -arch arm64 build
    )
}

cli() {
    "$JUCEWRIGHT" -s "$SESSION" "$@"
}

launch_app() {
    local label="$1"
    local suffix
    local launch_home
    local stdout_log
    local stderr_log
    local launch_log

    stop_app
    sleep 0.5

    LAUNCH_INDEX=$((LAUNCH_INDEX + 1))
    suffix="$(printf '%02d_%s' "$LAUNCH_INDEX" "$(slug "$label")")"
    launch_home="$AUTOMATION_HOME_ROOT/$suffix"
    mkdir -p "$launch_home"
    stdout_log="$ARTIFACT_DIR/osci-render.$suffix.stdout.log"
    stderr_log="$ARTIFACT_DIR/osci-render.$suffix.stderr.log"
    launch_log="$ARTIFACT_DIR/launch.$suffix.json"

    log "Launching osci-render ($label)"
    "$JUCEWRIGHT" launch \
        --app "$APP_BUNDLE" \
        --app-name "osci-render" \
        --session "$SESSION" \
        --artifact-dir "$ARTIFACT_DIR" \
        --home "$launch_home" \
        --source-home "$SOURCE_HOME" \
        --copy-setting "osci-licensing.settings" \
        --stdout "$stdout_log" \
        --stderr "$stderr_log" \
        --timeout-ms "$((SESSION_TIMEOUT_SECONDS * 1000))" \
        >"$launch_log" || die "Timed out waiting for jucewright session '$SESSION' (see $launch_log, $stderr_log)"

    if [[ -z "$APP_PID" ]]; then
        APP_PID="$(pgrep -x "osci-render" | tail -n 1 || true)"
    fi
    sleep 0.9
}

mcp_call() {
    local label="$1"
    local request="$2"
    STEP=$((STEP + 1))
    local file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_mcp_$(slug "$label").json"

    log "MCP $label"
    if printf '%s\n' "$request" | "$JUCEWRIGHT" mcp >"$file" 2>&1; then
        log "OK  MCP $label -> $file"
        return 0
    else
        local status=$?
        log "FAIL MCP $label -> $file"
        FAILURES+=("MCP $label (exit $status): $file")
        return "$status"
    fi
}

check_png_not_blank() {
    local file="$1"
    python3 - "$file" <<'PY'
import struct
import sys
import zlib

path = sys.argv[1]
data = open(path, "rb").read()
if not data.startswith(b"\x89PNG\r\n\x1a\n"):
    raise SystemExit("not a png")

pos = 8
width = height = bit_depth = color_type = interlace = None
payload = b""

while pos + 8 <= len(data):
    length = struct.unpack(">I", data[pos:pos + 4])[0]
    kind = data[pos + 4:pos + 8]
    chunk = data[pos + 8:pos + 8 + length]
    pos += 12 + length
    if kind == b"IHDR":
        width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
    elif kind == b"IDAT":
        payload += chunk
    elif kind == b"IEND":
        break

channels_by_type = {0: 1, 2: 3, 4: 2, 6: 4}
channels = channels_by_type.get(color_type)
if bit_depth != 8 or interlace != 0 or channels is None:
    raise SystemExit(0)

raw = zlib.decompress(payload)
stride = width * channels
prior = bytearray(stride)
values = []
offset = 0

for _ in range(height):
    filter_type = raw[offset]
    offset += 1
    row = bytearray(raw[offset:offset + stride])
    offset += stride
    recon = bytearray(stride)
    for x, value in enumerate(row):
        a = recon[x - channels] if x >= channels else 0
        b = prior[x]
        c = prior[x - channels] if x >= channels else 0
        if filter_type == 0:
            recon[x] = value
        elif filter_type == 1:
            recon[x] = (value + a) & 255
        elif filter_type == 2:
            recon[x] = (value + b) & 255
        elif filter_type == 3:
            recon[x] = (value + ((a + b) // 2)) & 255
        elif filter_type == 4:
            p = a + b - c
            pa = abs(p - a)
            pb = abs(p - b)
            pc = abs(p - c)
            pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            recon[x] = (value + pr) & 255
        else:
            raise SystemExit("unsupported png filter")
    prior = recon

    if color_type in (2, 6):
        step = channels
        values.extend(recon[i] for i in range(0, len(recon), step))
        values.extend(recon[i] for i in range(1, len(recon), step))
        values.extend(recon[i] for i in range(2, len(recon), step))
    else:
        values.extend(recon[0::channels])

if not values:
    raise SystemExit("empty png")

minimum = min(values)
maximum = max(values)
mean = sum(values) / len(values)
print(f"min={minimum} max={maximum} mean={mean:.2f}")
if maximum < 10 or maximum - minimum < 4:
    raise SystemExit("image appears blank or nearly flat")
PY
}

discover_visible_controls() {
    local snapshot_file="$1"
    local max_controls="${2:-0}"

    python3 - "$snapshot_file" "$max_controls" <<'PY'
import json
import math
import re
import sys

snapshot_path = sys.argv[1]
max_controls = int(sys.argv[2])
data = json.load(open(snapshot_path, "r", encoding="utf-8"))
root = data.get("tree", {})

skip_names = re.compile(
    r"(closeOverlay|Record output|Open Project|Save Project|Save Project As|"
    r"Open visualiser window|Toggle fullscreen visualiser|Shared texture output|"
    r"Open files and examples|Audio input|Website|Report Issue|Beta updates|"
    r"Download|Purchase|License|Reset|Add |Add$|Remove|Delete|Clear|Pause|"
    r"Play|Stop|Repeat|Randomise|Auto-link|Render Audio|Syphon|Spout)",
    re.IGNORECASE,
)

def actions(node):
    value = node.get("actions", [])
    return value if isinstance(value, list) else []

def label(node):
    for key in ("name", "title", "componentName", "componentId", "class"):
        value = str(node.get(key, "")).strip()
        if value:
            return value
    return node.get("ref", "unnamed")

def is_visible_control(node):
    if not node.get("visible", True):
        return False
    if not node.get("enabled", True):
        return False
    if node.get("role") == "ignored":
        return False
    if not node.get("ref"):
        return False
    name = label(node)
    if skip_names.search(name):
        return False
    bounds = node.get("bounds", {})
    if bounds.get("w", 1) <= 0 or bounds.get("h", 1) <= 0:
        return False
    return True

def numeric(value):
    try:
        number = float(value)
    except Exception:
        return None
    return number if math.isfinite(number) else None

def slider_value(node, index):
    minimum = numeric(node.get("minimum"))
    maximum = numeric(node.get("maximum"))
    current = numeric(node.get("value"))
    if minimum is None or maximum is None or maximum <= minimum:
        if current is None:
            return None
        return current + (0.1 if index % 2 == 0 else -0.1)

    fraction = 0.35 if index % 2 == 0 else 0.65
    value = minimum + (maximum - minimum) * fraction
    interval = numeric(node.get("interval"))
    if interval is not None and interval > 0:
        value = round(value / interval) * interval
    return value

def first_enabled_option_index(node):
    options = node.get("options", [])
    if not isinstance(options, list):
        return None

    current_text = str(node.get("value", ""))
    fallback = None
    for option in options:
        if not isinstance(option, dict):
            continue
        if not option.get("enabled", True):
            continue
        if option.get("separator", False) or option.get("sectionHeader", False):
            continue
        if option.get("hasSubMenu", False):
            continue
        index = option.get("index")
        if index is None:
            continue
        if fallback is None:
            fallback = index
        if current_text and str(option.get("text", "")) != current_text:
            return index
    return fallback

def walk(node):
    yield node
    for child in node.get("children", []) or []:
        if isinstance(child, dict):
            yield from walk(child)

emitted = 0
for node in walk(root):
    if max_controls and emitted >= max_controls:
        break
    if not is_visible_control(node):
        continue

    role = node.get("role", "")
    node_actions = actions(node)
    name = label(node).replace("\t", " ").replace("\n", " ")
    ref = node["ref"]

    if role == "slider":
        value = slider_value(node, emitted)
        if value is None:
            continue
        print(f"{ref}\t{role}\tset-value\t{value:.8g}\t{name}")
        emitted += 1
    elif role == "comboBox":
        index = first_enabled_option_index(node)
        if index is None:
            continue
        print(f"{ref}\t{role}\tselect-index\t{index}\t{name}")
        emitted += 1
    elif role in ("button", "toggleButton", "checkBox") and node.get("toggleable", False):
        desired = "false" if bool(node.get("checked", node.get("toggleState", False))) else "true"
        print(f"{ref}\t{role}\tset-checked\t{desired}\t{name}")
        emitted += 1
    elif role == "editableText" and not node.get("readOnly", False):
        print(f"{ref}\t{role}\tfill\tautomation\t{name}")
        emitted += 1
PY
}

exercise_visible_controls() {
    local label="$1"
    local max_controls="${2:-0}"
    shift 2

    STEP=$((STEP + 1))
    local snapshot_file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_dynamic_$(slug "$label")_snapshot.json"
    local controls_file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_dynamic_$(slug "$label")_controls.tsv"

    log "DISCOVER visible controls in $label"
    if ! cli snapshot --json --full --depth 18 "$@" >"$snapshot_file" 2>"$snapshot_file.stderr"; then
        log "SKIP/FAIL optional dynamic snapshot $label -> $snapshot_file.stderr"
        OPTIONAL_FAILURES+=("dynamic snapshot $label (exit 1): $snapshot_file.stderr")
        return 0
    fi

    if ! discover_visible_controls "$snapshot_file" "$max_controls" >"$controls_file" 2>"$controls_file.stderr"; then
        log "SKIP/FAIL optional dynamic discovery $label -> $controls_file.stderr"
        OPTIONAL_FAILURES+=("dynamic discovery $label (exit 1): $controls_file.stderr")
        return 0
    fi

    local controls_to_run="$controls_file"
    local name_filter="${DYNAMIC_NAME_FILTER:-}"
    if [[ -n "$name_filter" ]]; then
        local filtered_controls_file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_dynamic_$(slug "$label")_filtered_controls.tsv"
        awk -F $'\t' -v needle="$name_filter" 'BEGIN { OFS = FS; needle = tolower(needle) } index(tolower($5), needle) > 0 { print }' "$controls_file" >"$filtered_controls_file"
        controls_to_run="$filtered_controls_file"
    fi

    if [[ ! -s "$controls_to_run" ]]; then
        log "INFO no visible controls discovered for $label -> $controls_file"
        return 0
    fi

    local ref role action value name
    while IFS=$'\t' read -r ref role action value name; do
        case "$action" in
            set-value)
                try_step "dynamic $label set slider $name" cli set-value "$ref" --timeout-ms 3000 "$value"
                ;;
            select-index)
                try_step "dynamic $label select option $name" cli select-option "$ref" --index "$value" --timeout-ms 3000
                ;;
            set-checked)
                try_step "dynamic $label set checked $name" cli set-checked "$ref" --timeout-ms 3000 "$value"
                ;;
            fill)
                try_step "dynamic $label fill $name" cli fill "$ref" --timeout-ms 3000 "$value"
                ;;
        esac
    done <"$controls_to_run"
}

verify_new_recording_file() {
    local marker_file="$1"

    python3 - "$RECORDING_DIR" "$marker_file" <<'PY'
import os
import sys

recording_dir = sys.argv[1]
marker = sys.argv[2]
marker_time = os.path.getmtime(marker)
extensions = {".mp4", ".mov", ".wav", ".m4v"}
candidates = []

for root, _, files in os.walk(recording_dir):
    for name in files:
        path = os.path.join(root, name)
        ext = os.path.splitext(name)[1].lower()
        if ext not in extensions:
            continue
        stat = os.stat(path)
        if stat.st_mtime >= marker_time and stat.st_size > 512:
            candidates.append((stat.st_mtime, stat.st_size, path))

if not candidates:
    raise SystemExit(f"no recording file larger than 512 bytes was created in {recording_dir}")

candidates.sort(reverse=True)
for _, size, path in candidates[:3]:
    print(f"{path}\t{size} bytes")
PY
}

open_examples_panel() {
    try_step "open files and examples panel" cli click --name "openFiles" --exact --timeout-ms 5000
    try_step "examples panel snapshot" cli snapshot --json --interesting --depth 10
}

load_example() {
    local name="$1"
    local expect_source="${2:-0}"
    local nth
    nth="$(example_nth "$name")" || die "No example index mapping for '$name'"
    open_examples_panel
    run_step "load example $name" cli click --class "osci::GridItemComponent" --nth "$nth" --force --timeout-ms 6000
    run_step "wait after loading $name" cli wait --ms 400
    run_step "snapshot after loading $name" cli snapshot --json --interesting --depth 12
    if [[ "$expect_source" -eq 1 ]]; then
        try_step "source editor for $name" cli snapshot --json --interesting --depth 8 --class "ErrorCodeEditorComponent" --nth 0
    fi
    try_step "visualiser screenshot after $name" cli screenshot --class "VisualiserComponent" --nth 0 --source auto --file "$ARTIFACT_DIR/visualiser_$(slug "$name").png"
    try_step "dismiss transient overlays after $name" cli press Escape
}

exercise_current_file_common() {
    local label="$1"

    try_step "describe file controls for $label" cli describe --json --interesting --depth 8 --class "FileControlsComponent"
    try_step "describe quick controls for $label" cli describe --json --interesting --depth 8 --class "QuickControlsBar"
    try_step "randomise effects for $label" cli click --name "randomise" --exact --timeout-ms 5000
    try_step "toggle auto-link lfos for $label" cli click --name "autoLink" --exact --timeout-ms 3000
    try_step "restore auto-link lfos for $label" cli click --name "autoLink" --exact --timeout-ms 3000
    try_step "visualiser screenshot for $label" cli screenshot --class "VisualiserComponent" --nth 0 --source auto --file "$ARTIFACT_DIR/visualiser_common_$(slug "$label").png"
    exercise_visible_controls "quick controls for $label" 0 --class "QuickControlsBar"
}

exercise_text_file_controls() {
    local name="$1"

    try_step "text font describe for $name" cli describe --json --interesting --depth 8 --class "TxtComponent" --nth 0
    try_step "select text font for $name" cli select-option --class "TxtComponent" --nth 0 --index 0 --timeout-ms 3000
}

exercise_lua_file_controls() {
    local name="$1"

    try_step "lua source editor for $name" cli snapshot --json --interesting --depth 8 --class "ErrorCodeEditorComponent" --nth 0
    try_step "reset lua state for $name" cli click --name "luaReset" --exact --timeout-ms 3000
    try_step "open lua scripting reference for $name" cli click --name "luaHelp" --exact --timeout-ms 3000
    try_step "lua scripting reference snapshot for $name" cli snapshot --json --interesting --depth 10
    try_step "close lua scripting reference for $name" cli click --name "closeOverlay" --exact --timeout-ms 3000
    try_step "lua slider a for $name" cli set-value --role slider --name "Lua Slider A" --exact --timeout-ms 3000 0.25
    try_step "lua slider b for $name" cli set-value --role slider --name "Lua Slider B" --exact --timeout-ms 3000 0.75
    try_step "pause lua console for $name" cli click --name "pauseConsole" --exact --timeout-ms 3000
    try_step "clear lua console for $name" cli click --name "clearConsole" --exact --timeout-ms 3000
}

exercise_fractal_file_controls() {
    local name="$1"

    try_step "fractal editor describe for $name" cli describe --json --interesting --depth 10 --class "FractalComponent"
    exercise_visible_controls "fractal editor for $name" 0 --class "FractalComponent"
    try_step "add fractal rule for $name" cli click --name "+ Add Rule" --exact --timeout-ms 3000
}

exercise_frame_and_timeline_controls() {
    local label="$1"
    local kind="$2"

    if [[ "$kind" == "gpla" || "$kind" == "gif" || "$kind" == "mp4" || "$kind" == "mov" ]]; then
        try_step "frame settings describe for $label" cli describe --json --interesting --depth 10 --name "Frame settings" --exact
        try_step "set frames per second for $label" cli fill --role editableText --name "Frames per second" --exact --timeout-ms 3000 "12.00"
    fi

    if [[ "$kind" == "gif" || "$kind" == "png" || "$kind" == "jpg" || "$kind" == "mp4" || "$kind" == "mov" ]]; then
        try_step "toggle invert image for $label" cli click --role button --name "Invert Image" --exact --timeout-ms 3000
        try_step "set image threshold for $label" cli set-value --role slider --name "Image Threshold" --exact --timeout-ms 3000 0.4
        try_step "set image stride for $label" cli set-value --role slider --name "Image Stride" --exact --timeout-ms 3000 3
    fi

    if [[ "$kind" == "gpla" || "$kind" == "gif" || "$kind" == "flac" || "$kind" == "mp4" || "$kind" == "mov" ]]; then
        try_step "timeline describe for $label" cli describe --json --interesting --depth 8 --class "TimelineComponent"
        try_step "timeline pause for $label" cli click --name "Pause" --exact --timeout-ms 3000
        try_step "timeline play for $label" cli click --name "Play" --exact --timeout-ms 3000
        try_step "timeline repeat for $label" cli click --name "Repeat" --exact --timeout-ms 3000
        try_step "timeline pause after play for $label" cli click --name "Pause" --exact --timeout-ms 3000
        try_step "timeline stop for $label" cli click --name "Stop" --exact --timeout-ms 3000
    fi
}

exercise_file_switching() {
    try_step "switch to previous file" cli click --name "leftArrow" --exact --timeout-ms 3000
    try_step "snapshot after previous file" cli snapshot --json --interesting --depth 10
    try_step "switch to next file" cli click --name "rightArrow" --exact --timeout-ms 3000
    try_step "snapshot after next file" cli snapshot --json --interesting --depth 10
}

component_size() {
    local label="$1"
    shift
    local snapshot_file="$ARTIFACT_DIR/geometry_$(slug "$label").json"

    cli snapshot --json --interesting --depth 4 "$@" >"$snapshot_file" 2>"$snapshot_file.stderr" || return 1
    python3 - "$snapshot_file" <<'PY'
import json
import sys

data = json.load(open(sys.argv[1], "r", encoding="utf-8"))
root = data.get("tree", {})

def walk(node):
    yield node
    for child in node.get("children", []) or []:
        if isinstance(child, dict):
            yield from walk(child)

for node in walk(root):
    bounds = node.get("bounds", {})
    width = int(round(float(bounds.get("w", 0))))
    height = int(round(float(bounds.get("h", 0))))
    if width > 0 and height > 0:
        print(width, height)
        raise SystemExit(0)

raise SystemExit("component has no usable bounds")
PY
}

drag_component_percent() {
    local label="$1"
    local x_percent="$2"
    local y_percent="$3"
    local dx="$4"
    local dy="$5"
    shift 5

    local size width height x y
    if ! size="$(component_size "$label" "$@")"; then
        try_step "$label geometry unavailable" false
        return 0
    fi

    read -r width height <<<"$size"
    x=$((width * x_percent / 100))
    y=$((height * y_percent / 100))
    x=$((x < 1 ? 1 : x))
    y=$((y < 1 ? 1 : y))
    x=$((x >= width ? width - 1 : x))
    y=$((y >= height ? height - 1 : y))

    try_step "$label" cli drag "$@" --position "$x,$y" --dx "$dx" --dy "$dy" --steps 10 --timeout-ms 5000
}

exercise_midi_keyboard_clicks() {
    run_step "midi keyboard snapshot" cli snapshot --json --interesting --depth 8 --class "CustomMidiKeyboardComponent" --nth 0 --timeout-ms 5000
    run_step "midi keyboard low note click" cli click --class "CustomMidiKeyboardComponent" --nth 0 --position 16,24 --force --timeout-ms 3000
    run_step "midi keyboard middle note click" cli click --class "CustomMidiKeyboardComponent" --nth 0 --position 72,24 --force --timeout-ms 3000
    run_step "midi keyboard high note click" cli click --class "CustomMidiKeyboardComponent" --nth 0 --position 140,24 --force --timeout-ms 3000
    run_step "midi keyboard multi-note drag" cli drag --class "CustomMidiKeyboardComponent" --nth 0 --position 32,24 --dx 180 --dy 0 --steps 14 --force --timeout-ms 5000
    try_step "visualiser screenshot after midi key clicks" cli screenshot --class "VisualiserComponent" --nth 0 --source auto --file "$ARTIFACT_DIR/visualiser_after_midi_keyboard.png"
}

ensure_midi_keyboard_visible() {
    STEP=$((STEP + 1))
    local file="$ARTIFACT_DIR/$(printf '%03d' "$STEP")_probe_midi_keyboard_visible.log"

    log "PROBE MIDI keyboard visible"
    if cli snapshot --json --interesting --depth 8 --class "CustomMidiKeyboardComponent" --nth 0 --timeout-ms 2000 >"$file" 2>&1; then
        log "OK  MIDI keyboard already visible -> $file"
        return 0
    fi

    log "INFO MIDI keyboard hidden; enabling Interface > Show MIDI Keyboard -> $file"
    run_step "show midi keyboard preference" select_menu_item "Show MIDI Keyboard"
    run_step "wait after showing midi keyboard preference" cli wait --ms 500
}

exercise_modulation_graph_handles_for_tab() {
    local tab="$1"

    if [[ "$tab" == RAND* ]]; then
        run_step "random graph drag for $tab" cli drag --class RandomGraphComponent --nth 0 --position 80,24 --dx 50 --dy 0 --steps 8 --timeout-ms 5000
        return 0
    fi

    if [[ "$tab" == INPUT ]]; then
        try_step "input graph drag first handle" cli drag --class NodeGraphComponent --nth 2 --position 18,90 --dx 20 --dy -20 --steps 8 --timeout-ms 5000 --force
        try_step "input graph drag second handle" cli drag --class NodeGraphComponent --nth 2 --position 105,44 --dx -14 --dy 18 --steps 8 --timeout-ms 5000 --force
        return 0
    fi

    for graph_nth in 0 1 2; do
        drag_component_percent "drag first graph handle for $tab graph $graph_nth" 8 65 24 -18 --class NodeGraphComponent --nth "$graph_nth"
        drag_component_percent "drag second graph handle for $tab graph $graph_nth" 50 35 -12 20 --class NodeGraphComponent --nth "$graph_nth"
        drag_component_percent "drag curve graph handle for $tab graph $graph_nth" 35 45 0 24 --class NodeGraphComponent --nth "$graph_nth"
    done
}

mod_drag_description() {
    local tab="$1"
    local prefix index

    case "$tab" in
        LFO\ *)
            prefix="LFO"
            index="${tab#LFO }"
            ;;
        ENV\ *)
            prefix="ENV"
            index="${tab#ENV }"
            ;;
        RAND\ *)
            prefix="RNG"
            index="${tab#RAND }"
            ;;
        INPUT)
            prefix="SC"
            index="1"
            ;;
        *)
            return 1
            ;;
    esac

    printf 'MOD:%s:%d' "$prefix" "$((index - 1))"
}

exercise_modulation_source_assignment() {
    local tab="$1"
    local drag_description
    local handle_nth
    drag_description="$(mod_drag_description "$tab")"
    handle_nth="$(mod_handle_nth "$tab")" || die "No modulation handle index mapping for '$tab'"

    run_step "select modulation tab for assignment $tab" cli click --class "ModTabHandle" --nth "$handle_nth" --force --timeout-ms 3000
    run_step "drag modulation source $tab to perspective" cli drag-to --class "ModTabHandle" --nth "$handle_nth" --target-class "KnobContainerComponent" --target-nth 1 --force --steps 16 --timeout-ms 6000
    run_step "drop modulation source $tab on perspective" cli drop --class "KnobContainerComponent" --nth 1 --description "$drag_description" --force --timeout-ms 5000
    run_step "wait after modulation drop $tab" cli wait --ms 250
    run_step "snapshot modulation assignment $tab" cli snapshot --json --interesting --depth 10 --class "ModTabHandle" --nth "$handle_nth"
    run_step "drag modulation strength $tab" cli drag --class "DepthIndicator" --nth 0 --dx 0 --dy -18 --steps 8 --force --timeout-ms 5000
    run_step "right click modulation strength $tab and set bipolar" cli right-click --class "DepthIndicator" --nth 0 --force --menu-item "Make Bipolar" --timeout-ms 5000
    try_step "dismiss modulation context menu $tab" cli press Escape
}

exercise_hello_world_editor() {
    local edited_text=$'hello\njucewright automation\nworld'

    run_step "edit Hello World source text" cli fill --class "ErrorCodeEditorComponent" --nth 0 --timeout-ms 5000 "$edited_text"
    run_step "wait after editing Hello World source text" cli wait --ms 500
    run_step "snapshot after editing Hello World source text" cli snapshot --json --interesting --depth 8 --class "ErrorCodeEditorComponent" --nth 0
    run_step "collapse Hello World source editor" cli click --name "Collapse" --exact --timeout-ms 3000
    run_step "snapshot after collapsing Hello World source editor" cli snapshot --json --interesting --depth 12
}

exercise_effect() {
    local effect="$1"
    local nth
    nth="$(effect_nth "$effect")" || die "No effect index mapping for '$effect'"
    try_open_effect_browser "$effect"
    run_step "find effect browser item $effect" cli locator --class "osci::GridItemComponent" --nth "$nth" --timeout-ms 5000
    run_step "add effect $effect" cli click --class "osci::GridItemComponent" --nth "$nth" --force --timeout-ms 6000
    try_step "wait after adding effect $effect" cli wait --ms 200
    try_step "snapshot selected effect $effect" cli snapshot --json --interesting --depth 10 --class "DraggableListBox"
    exercise_visible_controls "selected effect $effect" 18 --class "DraggableListBox"
}

exercise_menu() {
    local menu="$1"
    try_step "open menu $menu" cli click --role menuItem --name "$menu" --exact --timeout-ms 3000
    try_step "snapshot menu $menu" cli snapshot --json --full --depth 8
    try_step "close menu $menu" cli press Escape
}

select_menu_item() {
    local item="$1"
    cli select-option --role menuBar --class "MenuBarComponent" --text "$item" --timeout-ms 5000
}

close_overlay() {
    cli click --role button --name "closeOverlay" --exact --timeout-ms 3000
    local status=$?
    cli wait --ms 250 >/dev/null 2>&1 || true
    return "$status"
}

exercise_about_dialog() {
    run_step "open about dialog" select_menu_item "About osci-render"
    run_step "about dialog snapshot" cli snapshot --json --interesting --depth 12
    try_step "about website button trial" cli click --name "Website" --exact --trial --timeout-ms 3000
    try_step "about report issue button trial" cli click --name "Report Issue" --exact --trial --timeout-ms 3000
    run_step "close about dialog" close_overlay
}

exercise_license_dialog() {
    try_step "open license and updates dialog" select_menu_item "License and Updates..."
    try_step "license and updates snapshot" cli snapshot --json --interesting --depth 12
    try_step "license and updates full snapshot" cli snapshot --json --full --depth 14
    try_step "close license and updates dialog" close_overlay
}

exercise_recording_dialog() {
    run_step "open recording settings dialog" select_menu_item "Recording Settings..."
    run_step "recording settings snapshot" cli snapshot --json --interesting --depth 12
    try_step "set recording video quality" cli set-value --role slider --name "Video Quality" --exact --timeout-ms 3000 0.62
    try_step "set recording resolution" cli set-value --role slider --name "Resolution" --exact --timeout-ms 3000 720
    try_step "set recording frame rate" cli set-value --role slider --name "Frame Rate" --exact --timeout-ms 3000 30
    try_step "toggle lossless video" cli click --role button --name "Lossless Video" --exact --timeout-ms 3000
    try_step "select compression speed medium" cli select-option --role comboBox --name "Compression speed" --exact --text "medium" --timeout-ms 3000
    try_step "select video codec h265" cli select-option --role comboBox --name "Video codec" --exact --text "H.265/HEVC" --timeout-ms 3000
    try_step "set custom shared texture output name" cli fill --role editableText --name "Custom Syphon/Spout name" --exact --timeout-ms 3000 "osci-render-automation"
    exercise_visible_controls "recording settings dialog" 0 --class "RecordingSettingsOverlay"
    run_step "close recording settings dialog" close_overlay
}

exercise_audio_dialog() {
    run_step "open audio settings dialog" select_menu_item "Settings..."
    run_step "wait for audio settings overlay animation" cli wait --ms 1200
    run_step "audio settings snapshot" cli snapshot --json --interesting --depth 14
    try_step "audio settings full snapshot" cli snapshot --json --full --depth 16
    exercise_visible_controls "audio settings dialog" 0 --class "AudioSettingsOverlay"
    run_step "close audio settings dialog" close_overlay
}

exercise_visualiser_settings_dialog() {
    run_step "open visualiser settings dialog" cli click --name "settings" --timeout-ms 3000
    run_step "visualiser settings snapshot" cli snapshot --json --interesting --depth 12
    exercise_visible_controls "visualiser settings dialog" 0 --role dialogWindow --name "Visualiser Settings" --exact
    run_step "close visualiser settings dialog" cli press Escape --role dialogWindow --name "Visualiser Settings" --exact --timeout-ms 3000
}

exercise_application_dialogs() {
    exercise_about_dialog
    exercise_license_dialog
    exercise_recording_dialog
    exercise_audio_dialog
    exercise_visualiser_settings_dialog
    try_step "syphon spout input dialog" select_menu_item "Select Syphon/Spout Input..."
    try_step "syphon spout input snapshot" cli snapshot --json --interesting --depth 12
    exercise_visible_controls "syphon spout input dialog" 0 --role dialogWindow --name "Select Syphon/Spout Input" --exact
    try_step "close syphon spout input dialog" close_overlay
}

configure_visualiser_recording_for_automation() {
    run_step "open recording settings for visualiser recording" select_menu_item "Recording Settings..."
    try_step "recording uses h264" cli select-option --role comboBox --name "Video codec" --text "H.264" --timeout-ms 3000
    try_step "recording uses ultrafast compression" cli select-option --role comboBox --name "Compression speed" --text "ultrafast" --timeout-ms 3000
    try_step "recording disables lossless video" cli set-checked --role button --name "Lossless Video" --exact --timeout-ms 3000 false
    try_step "recording enables video" cli set-checked --role button --name "Record Video" --exact --timeout-ms 3000 true
    try_step "recording disables audio mux" cli set-checked --role button --name "Record Audio" --exact --timeout-ms 3000 false
    try_step "recording resolution low" cli set-value --role slider --name "Resolution" --exact --timeout-ms 3000 256
    try_step "recording frame rate low" cli set-value --role slider --name "Frame Rate" --exact --timeout-ms 3000 10
    try_step "recording video quality medium" cli set-value --role slider --name "Video Quality" --exact --timeout-ms 3000 0.45
    run_step "close recording settings for visualiser recording" close_overlay
}

exercise_visualiser_recording() {
    local marker_file="$ARTIFACT_DIR/visualiser-recording-start.marker"

    if [[ "$INCLUDE_NATIVE" -ne 1 ]]; then
        try_step "visualiser recording skipped because stopping opens a native save dialog" true
        return 0
    fi

    configure_visualiser_recording_for_automation
    : >"$marker_file"
    run_step "start visualiser recording" cli click --name "Record" --exact --timeout-ms 5000
    run_step "wait while recording visualiser" cli wait --ms 2600
    try_step "visualiser screenshot during recording" cli screenshot --class "VisualiserComponent" --nth 0 --source auto --file "$ARTIFACT_DIR/visualiser_recording_active.png"
    run_step "stop visualiser recording" cli click --name "Record" --exact --timeout-ms 5000
    try_step "cancel native recording save dialog" native_escape
    run_step "wait after cancelling visualiser recording save dialog" cli wait --ms 1000
    try_step "verify visualiser recording file if native dialog saved" verify_new_recording_file "$marker_file"
}

exercise_native_dialog_entries() {
    try_step "render audio file to video native dialog trial" select_menu_item "Render Audio File to Video..."
    try_step "close native dialog with escape" cli press Escape
}

prepare_external_fixtures() {
    FIXTURE_DIR="$ARTIFACT_DIR/fixtures"
    mkdir -p "$FIXTURE_DIR"

    cp "$ROOT_DIR/Resources/gpla/fallback.gpla" "$FIXTURE_DIR/automation.gpla"
    cp "$ROOT_DIR/images/demo1.gif" "$FIXTURE_DIR/automation.gif"
    cp "$ROOT_DIR/Resources/oscilloscope/real.png" "$FIXTURE_DIR/automation.png"
    cp "$ROOT_DIR/Resources/oscilloscope/empty.jpg" "$FIXTURE_DIR/automation.jpg"
    cp "$ROOT_DIR/Resources/audio/sosci.flac" "$FIXTURE_DIR/automation.flac"
    cp "$ROOT_DIR/osci-render-website/assets/images/cubes.mp4" "$FIXTURE_DIR/automation.mp4"
    cp "$ROOT_DIR/osci-render-website/assets/images/cubes.mp4" "$FIXTURE_DIR/automation.mov"
}

exercise_external_file() {
    local kind="$1"
    local path="$2"

    launch_app "external $kind"
    run_step "drop external $kind file" cli drop-files --file "$path" --timeout-ms 5000
    run_step "wait after dropping external $kind" cli wait --ms 750
    run_step "snapshot external $kind" cli snapshot --json --interesting --depth 12
    run_step "visualiser screenshot external $kind" cli screenshot --class "VisualiserComponent" --nth 0 --source auto --file "$ARTIFACT_DIR/visualiser_external_$(slug "$kind").png"
    try_step "visualiser nonblank external $kind" check_png_not_blank "$ARTIFACT_DIR/visualiser_external_$(slug "$kind").png"
    exercise_current_file_common "external $kind"
    exercise_frame_and_timeline_controls "external $kind" "$kind"
}

exercise_external_file_type_passes() {
    prepare_external_fixtures

    local external_types=("gpla" "gif" "png" "jpg" "flac" "mp4" "mov")
    if [[ "$QUICK" -eq 1 ]]; then
        external_types=("gpla" "gif" "flac")
    fi

    for kind in "${external_types[@]}"; do
        exercise_external_file "$kind" "$FIXTURE_DIR/automation.$kind"
    done
}

if ! find_jucewright; then
    build_jucewright || die "Failed to build jucewright"
    find_jucewright || die "Could not find jucewright after build"
fi

if [[ "$BUILD_APP" -eq 1 || ! -x "$APP_EXECUTABLE" ]]; then
    build_app || die "Failed to build osci-render"
fi

[[ -x "$APP_EXECUTABLE" ]] || die "App executable not found: $APP_EXECUTABLE"
[[ -x "$JUCEWRIGHT" ]] || die "jucewright not executable: $JUCEWRIGHT"

log "Artifacts: $ARTIFACT_DIR"
log "jucewright: $JUCEWRIGHT"
log "App: $APP_EXECUTABLE"

launch_app "clean startup"

run_step "list sessions" "$JUCEWRIGHT" list
run_step "capabilities" cli capabilities
run_step "windows" cli windows
run_step "start trace" cli trace-start --file "$ARTIFACT_DIR/trace.json"

run_step "root full snapshot" cli snapshot --json --full --depth 14
run_step "root interesting snapshot" cli snapshot --json --interesting --depth 12
run_step "root screenshot" cli screenshot --target root --source auto --file "$ARTIFACT_DIR/root.png"
run_step "visualiser count" cli count --class "VisualiserComponent" --nth 0
run_step "visualiser describe" cli describe --json --interesting --depth 8 --class "VisualiserComponent" --nth 0
run_step "visualiser screenshot" cli screenshot --class "VisualiserComponent" --nth 0 --source auto --file "$ARTIFACT_DIR/visualiser.png"
run_step "visualiser screenshot nonblank check" check_png_not_blank "$ARTIFACT_DIR/visualiser.png"

for menu in File Edit About Video Audio Interface; do
    exercise_menu "$menu"
done

exercise_application_dialogs

run_step "file controls describe" cli describe --json --interesting --depth 8 --class "FileControlsComponent"
run_step "quick controls describe" cli describe --json --interesting --depth 8 --class "QuickControlsBar"
run_step "volume controls describe" cli describe --json --interesting --depth 8 --class "VolumeComponent"
exercise_visible_controls "quick controls root" 0 --class "QuickControlsBar"
exercise_visible_controls "volume controls root" 0 --class "VolumeComponent"
if [[ "$QUICK" -eq 1 ]]; then
    exercise_visible_controls "main workspace controls" 25
else
    exercise_visible_controls "main workspace controls" 120
fi

TEXT_EXAMPLES=("Hello World" "Greek" "osci-render" "Paperclip" "sosci")
LUA_EXAMPLES=("Spiral" "Shape Generator" "Squiggles" "Donut" "Graph" "Gravity Well" "Helix" "Human" "Hypercube" "Mushroom" "Planet")
MODEL_EXAMPLES=("Cube" "Diamond" "Dodecahedron" "Humanoid Quad" "Icosahedron" "Lamp" "Shuttle" "Suzanne" "Teapot" "Tetrahedron")
SVG_EXAMPLES=("Air Horn" "Alien" "Bicycle" "Card" "Cash" "Cat" "Clippy" "Desktop" "Puzzle" "Skull" "Snowflake" "Yin Yang")
FRACTAL_EXAMPLES=("Koch Snowflake" "Sierpinski Triangle" "Dragon Curve" "Binary Tree" "Hilbert Curve")
if [[ "$QUICK" -eq 1 ]]; then
    TEXT_EXAMPLES=("Hello World")
    LUA_EXAMPLES=("Spiral" "Shape Generator")
    MODEL_EXAMPLES=("Cube")
    SVG_EXAMPLES=("Air Horn")
    FRACTAL_EXAMPLES=("Koch Snowflake")
fi

for example in "${MODEL_EXAMPLES[@]}"; do
    load_example "$example"
    exercise_current_file_common "model $example"
done

for example in "${LUA_EXAMPLES[@]}"; do
    load_example "$example" 1
    exercise_current_file_common "lua $example"
    exercise_lua_file_controls "$example"
done

for example in "${SVG_EXAMPLES[@]}"; do
    load_example "$example"
    exercise_current_file_common "svg $example"
done

for example in "${FRACTAL_EXAMPLES[@]}"; do
    load_example "$example"
    exercise_current_file_common "fractal $example"
    exercise_fractal_file_controls "$example"
done

for example in "${TEXT_EXAMPLES[@]}"; do
    load_example "$example" 1
    exercise_current_file_common "text $example"
    exercise_text_file_controls "$example"
    if [[ "$example" == "Hello World" ]]; then
        exercise_hello_world_editor
    fi
done

exercise_file_switching

try_step "hidden timeline describe without animatable file" cli describe --json --interesting --depth 8 --class "TimelineComponent" --hidden

run_step "midi controls describe" cli describe --json --interesting --depth 8 --class "MidiComponent"
run_step "enable midi mode" cli set-checked --role button --name "MIDI" --exact --timeout-ms 3000 true
run_step "wait after enabling midi mode" cli wait --ms 500
run_step "midi controls after toggle" cli snapshot --json --interesting --depth 10 --class "MidiComponent"
ensure_midi_keyboard_visible
exercise_midi_keyboard_clicks

run_step "modulation tabs snapshot" cli snapshot --json --interesting --depth 10 --class "ModTabHandle"
MOD_TABS=("LFO 1" "LFO 2" "LFO 3" "LFO 4" "LFO 5" "LFO 6" "LFO 7" "LFO 8" "RAND 1" "RAND 2" "RAND 3" "INPUT" "ENV 1" "ENV 2" "ENV 3" "ENV 4" "ENV 5")
if [[ "$QUICK" -eq 1 ]]; then
    MOD_TABS=("LFO 1" "RAND 1" "INPUT" "ENV 1")
fi

for tab in "${MOD_TABS[@]}"; do
    handle_nth="$(mod_handle_nth "$tab")" || die "No modulation handle index mapping for '$tab'"
    try_step "select modulation tab $tab" cli click --class "ModTabHandle" --nth "$handle_nth" --force --timeout-ms 2500
    try_step "describe modulation tab $tab" cli describe --json --interesting --depth 8 --class "ModTabHandle" --nth "$handle_nth"
    exercise_modulation_graph_handles_for_tab "$tab"
    exercise_modulation_source_assignment "$tab"
done

try_step "lfo graph drag" cli drag --class NodeGraphComponent --nth 0 --dx 20 --dy -20 --steps 8 --timeout-ms 3000
try_step "modulation graph snapshot" cli snapshot --json --interesting --depth 10 --class NodeGraphComponent --nth 0

run_step "stop startup trace before clean effects sweep" cli trace-stop
launch_app "clean effects sweep"
run_step "start effects trace" cli trace-start --file "$ARTIFACT_DIR/effects-trace.json"
run_step "audio effects describe" cli describe --json --interesting --depth 10 --class "EffectsComponent"
try_open_effect_browser "initial effects sweep"
run_step "effect browser snapshot" cli snapshot --json --interesting --depth 10 --class "EffectTypeGridComponent"

EFFECTS=("Bit Crush" "Bounce" "Bulge" "Dash" "Delay" "Distort" "Duplicator" "God Ray" "Kaleidoscope" "Lua Effect" "Multiplex" "Polygonizer" "Ripple" "Rotate" "Scale" "Skew" "Smoothing" "Spiral Bit Crush" "Swirl" "Trace" "Translate" "Twist" "Unfold" "Vector Cancelling" "Vortex" "Wobble")
if [[ "$QUICK" -eq 1 ]]; then
    EFFECTS=("Bit Crush" "Rotate" "Scale")
fi

for effect in "${EFFECTS[@]}"; do
    exercise_effect "$effect"
done

try_step "visualiser settings button" cli click --name "settings" --timeout-ms 3000
try_step "visualiser settings snapshot" cli snapshot --json --interesting --depth 12
try_step "close visualiser settings" cli press Escape
exercise_visualiser_recording
try_step "shared texture output describe" cli describe --json --interesting --depth 6 --name "Syphon/Spout"
try_step "open visualiser window trial" cli click --name "new window" --trial --timeout-ms 3000
try_step "fullscreen trial" cli click --name "fullscreen" --trial --timeout-ms 3000

if [[ "$INCLUDE_NATIVE" -eq 1 ]]; then
    try_step "native open project trial" cli click --text "Open Project" --exact --trial --timeout-ms 3000
    try_step "native save project as trial" cli click --text "Save Project As" --exact --trial --timeout-ms 3000
    exercise_native_dialog_entries
fi

run_step "stop effects trace before external file passes" cli trace-stop
exercise_external_file_type_passes
run_step "start final trace" cli trace-start --file "$ARTIFACT_DIR/final-trace.json"

mcp_call "initialize" '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"osci-render-browser-script","version":"1.0.0"}}}'
mcp_call "tools list" '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
mcp_call "list sessions" '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"juce_list_sessions","arguments":{}}}'
mcp_call "snapshot" "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"juce_snapshot\",\"arguments\":{\"session\":\"$SESSION\",\"mode\":\"interesting\",\"depth\":10,\"format\":\"json\"}}}"
mcp_call "visualiser screenshot" "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"juce_screenshot\",\"arguments\":{\"session\":\"$SESSION\",\"locator\":{\"class\":\"VisualiserComponent\",\"nth\":0},\"file\":\"$ARTIFACT_DIR/mcp_visualiser.png\",\"includeBase64\":false}}}"

run_step "stop trace" cli trace-stop
run_step "final root snapshot" cli snapshot --json --interesting --depth 12

{
    printf '{\n'
    printf '  "session": "%s",\n' "$SESSION"
    printf '  "artifactDir": "%s",\n' "$ARTIFACT_DIR"
    printf '  "quick": %s,\n' "$([[ "$QUICK" -eq 1 ]] && printf true || printf false)"
    printf '  "failures": %d,\n' "${#FAILURES[@]}"
    printf '  "optionalFailures": %d\n' "${#OPTIONAL_FAILURES[@]}"
    printf '}\n'
} >"$ARTIFACT_DIR/summary.json"

if [[ "${#OPTIONAL_FAILURES[@]}" -gt 0 ]]; then
    log "Optional failures:"
    printf '  - %s\n' "${OPTIONAL_FAILURES[@]}" | tee "$ARTIFACT_DIR/optional-failures.txt"
fi

if [[ "${#FAILURES[@]}" -gt 0 ]]; then
    log "Failures:"
    printf '  - %s\n' "${FAILURES[@]}" | tee "$ARTIFACT_DIR/failures.txt"
    log "Summary: $ARTIFACT_DIR/summary.json"
    exit 1
fi

log "Completed without required failures. Summary: $ARTIFACT_DIR/summary.json"
