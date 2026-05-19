#!/usr/bin/env python3
from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import json
import math
import os
import platform
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path


TEXT_EXAMPLES = ["Hello World", "Greek", "osci-render", "Paperclip", "sosci"]
LUA_EXAMPLES = ["Spiral", "Shape Generator", "Squiggles", "Donut", "Graph", "Gravity Well", "Helix", "Human", "Hypercube", "Mushroom", "Planet"]
MODEL_EXAMPLES = ["Cube", "Diamond", "Dodecahedron", "Humanoid Quad", "Icosahedron", "Lamp", "Shuttle", "Suzanne", "Teapot", "Tetrahedron"]
SVG_EXAMPLES = ["Air Horn", "Alien", "Bicycle", "Card", "Cash", "Cat", "Clippy", "Desktop", "Puzzle", "Skull", "Snowflake", "Yin Yang"]
FRACTAL_EXAMPLES = ["Koch Snowflake", "Sierpinski Triangle", "Dragon Curve", "Binary Tree", "Hilbert Curve"]
EFFECTS = ["Bit Crush", "Bounce", "Bulge", "Dash", "Delay", "Distort", "Duplicator", "God Ray", "Kaleidoscope", "Lua Effect", "Multiplex", "Polygonizer", "Ripple", "Rotate", "Scale", "Skew", "Smoothing", "Spiral Bit Crush", "Swirl", "Trace", "Translate", "Twist", "Unfold", "Vector Cancelling", "Vortex", "Wobble"]
MOD_TABS = ["LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "RAND 1", "RAND 2", "RAND 3", "INPUT", "ENV 1", "ENV 2", "ENV 3", "ENV 4", "ENV 5"]

EXAMPLE_INDEX = {name: index for index, name in enumerate([
    "Hello World", "Greek", "osci-render", "Paperclip", "sosci", "Spiral", "Shape Generator", "Squiggles", "Donut", "Graph",
    "Gravity Well", "Helix", "Human", "Hypercube", "Mushroom", "Planet", "Cube", "Diamond", "Dodecahedron",
    "Humanoid Quad", "Icosahedron", "Lamp", "Shuttle", "Suzanne", "Teapot", "Tetrahedron", "Air Horn", "Alien",
    "Bicycle", "Card", "Cash", "Cat", "Clippy", "Desktop", "Puzzle", "Skull", "Snowflake", "Yin Yang",
    "Koch Snowflake", "Sierpinski Triangle", "Dragon Curve", "Binary Tree", "Hilbert Curve",
])}

EFFECT_INDEX = {name: index for index, name in enumerate(EFFECTS)}

MOD_HANDLE_INDEX = {
    "ENV 1": 0,
    "ENV 2": 1,
    "ENV 3": 2,
    "ENV 4": 3,
    "ENV 5": 4,
    "LFO 1": 5,
    "LFO 2": 6,
    "LFO 3": 7,
    "LFO 4": 8,
    "LFO 5": 9,
    "LFO 6": 10,
    "LFO 7": 11,
    "LFO 8": 12,
    "RAND 1": 13,
    "RAND 2": 14,
    "RAND 3": 15,
    "INPUT": 16,
}

SKIP_CONTROL_NAMES = re.compile(
    r"(closeOverlay|Record output|Open Project|Save Project|Save Project As|"
    r"Open visualiser window|Toggle fullscreen visualiser|Shared texture output|"
    r"Open files and examples|Audio input|inputEnabled|midi|Website|Report Issue|Beta updates|"
    r"Download|Purchase|License|Reset|Add |Add$|Remove|Delete|Clear|Pause|"
    r"Play|Stop|Repeat|Record|Randomise|Auto-link|Render Audio|sharedTexture|Syphon|Spout)",
    re.IGNORECASE,
)


class StepError(RuntimeError):
    pass


def is_windows() -> bool:
    return platform.system().lower() == "windows"


def is_macos() -> bool:
    return platform.system().lower() == "darwin"


def is_linux() -> bool:
    return platform.system().lower() == "linux"


def executable_name(name: str) -> str:
    return f"{name}.exe" if is_windows() else name


def slug(text: str) -> str:
    value = re.sub(r"[^A-Za-z0-9]+", "_", text.lower()).strip("_")
    return value or "step"


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def user_cache_root() -> Path:
    if is_macos():
        return Path.home() / "Library" / "Caches"
    if is_windows():
        return Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))


def default_build_dir() -> Path:
    return Path(tempfile.gettempdir()) / "jucewright-osci-render-cli"


def default_app_path(root: Path) -> Path:
    if is_macos():
        return root / "Builds" / "osci-render" / "MacOSX" / "build" / "Debug" / "osci-render.app"
    if is_windows():
        return root / "Builds" / "osci-render" / "VisualStudio2022" / "x64" / "Debug" / "Standalone Plugin" / "osci-render.exe"
    return root / "Builds" / "osci-render" / "LinuxMakefile" / "build" / "osci-render"


def default_app_executable(app_path: Path) -> Path:
    if is_macos() and app_path.suffix == ".app":
        return app_path / "Contents" / "MacOS" / "osci-render"
    return app_path


def is_executable(path: Path) -> bool:
    if not path.is_file():
        return False
    return is_windows() or os.access(path, os.X_OK)


def process_exists(pid: int) -> bool:
    if pid <= 0:
        return False

    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False


def numeric(value: object) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def walk_tree(node: dict):
    yield node
    for child in node.get("children", []) or []:
        if isinstance(child, dict):
            yield from walk_tree(child)


class BrowserRun:
    def __init__(self, args: argparse.Namespace):
        self.root_dir = Path(__file__).resolve().parents[1]
        self.session = args.session or os.environ.get("SESSION", "osci-render")
        self.session_timeout_seconds = int(os.environ.get("SESSION_TIMEOUT_SECONDS", "120"))

        default_artifact_root = user_cache_root() / "osci-render" / "osci-render-jucewright-automation"
        artifact_root = Path(os.environ.get("AUTOMATION_ARTIFACT_ROOT", default_artifact_root))
        default_artifact_dir = artifact_root / "browse" / dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.artifact_dir = Path(args.artifact_dir or os.environ.get("ARTIFACT_DIR", default_artifact_dir)).resolve()
        self.recording_dir = Path(os.environ.get("RECORDING_DIR", self.artifact_dir / "recordings")).resolve()
        self.automation_home_root = Path(os.environ.get("AUTOMATION_HOME_ROOT", self.artifact_dir / "home")).resolve()
        self.source_home = Path(os.environ.get("SOURCE_HOME", Path.home())).resolve()

        app_path_env = os.environ.get("APP_PATH") or os.environ.get("APP_BUNDLE")
        self.app_path = Path(args.app_path or app_path_env or default_app_path(self.root_dir)).resolve()
        self.app_executable = Path(args.app_executable or os.environ.get("APP_EXECUTABLE", default_app_executable(self.app_path))).resolve()
        self.jucewright = Path(args.jucewright or os.environ.get("JUCEWRIGHT", "")).resolve() if (args.jucewright or os.environ.get("JUCEWRIGHT")) else None
        self.jucewright_build_dir = Path(os.environ.get("JUCEWRIGHT_BUILD_DIR", default_build_dir())).resolve()

        self.build_app_requested = args.build_app
        self.quick = args.quick
        self.keep_app = args.keep_app
        self.include_native = args.native_dialogs
        self.step = 0
        self.launch_index = 0
        self.app_pid: int | None = None
        self.failures: list[str] = []
        self.optional_failures: list[str] = []
        self.fixture_dir = self.artifact_dir / "fixtures"

        self.artifact_dir.mkdir(parents=True, exist_ok=True)
        self.recording_dir.mkdir(parents=True, exist_ok=True)
        self.automation_home_root.mkdir(parents=True, exist_ok=True)

    def log(self, message: str) -> None:
        print(f"[browse-osci-render] {message}", flush=True)

    def jw(self, *args: object) -> list[str]:
        if self.jucewright is None:
            raise StepError("jucewright executable has not been resolved")
        return [str(self.jucewright), *[str(arg) for arg in args]]

    def cli(self, *args: object) -> list[str]:
        return self.jw("-s", self.session, *args)

    def call(self, command: list[str], stdin: str | None = None) -> str:
        completed = subprocess.run(command, cwd=self.root_dir, input=stdin, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if completed.stdout:
            print(completed.stdout, end="")
        if completed.returncode != 0:
            raise StepError(f"command exited {completed.returncode}: {' '.join(command)}")
        return completed.stdout

    def call_to_file(self, command: list[str], stdout_file: Path, stderr_file: Path | None = None, stdin: str | None = None) -> bool:
        stdout_file.parent.mkdir(parents=True, exist_ok=True)
        if stderr_file is not None:
            stderr_file.parent.mkdir(parents=True, exist_ok=True)

        with stdout_file.open("w", encoding="utf-8") as out:
            if stderr_file is None:
                completed = subprocess.run(command, cwd=self.root_dir, input=stdin, text=True, stdout=out, stderr=subprocess.STDOUT)
            else:
                with stderr_file.open("w", encoding="utf-8") as err:
                    completed = subprocess.run(command, cwd=self.root_dir, input=stdin, text=True, stdout=out, stderr=err)

        return completed.returncode == 0

    def next_step_file(self, label: str, prefix: str = "", suffix: str = ".log") -> Path:
        self.step += 1
        stem = f"{self.step:03d}_{prefix + '_' if prefix else ''}{slug(label)}"
        return self.artifact_dir / f"{stem}{suffix}"

    def step_action(self, label: str, action, required: bool, prefix: str = "", suffix: str = ".log") -> bool:
        file = self.next_step_file(label, prefix, suffix)
        verb = "RUN" if required else "TRY"
        self.log(f"{verb} {label}")

        try:
            with file.open("w", encoding="utf-8") as out, contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                if callable(action):
                    result = action()
                    if isinstance(result, str):
                        print(result, end="" if result.endswith("\n") else "\n")
                else:
                    self.call([str(part) for part in action])
        except Exception as exc:
            status = getattr(exc, "returncode", 1)
            if required:
                self.log(f"FAIL {label} -> {file}")
                self.failures.append(f"{label} (exit {status}): {file}")
            else:
                self.log(f"SKIP/FAIL optional {label} -> {file}")
                self.optional_failures.append(f"{label} (exit {status}): {file}")
            return False

        self.log(f"OK  {label} -> {file}")
        return True

    def run_step(self, label: str, action) -> bool:
        return self.step_action(label, action, True)

    def try_step(self, label: str, action) -> bool:
        return self.step_action(label, action, False, "optional")

    def optional_failure(self, label: str, message: str) -> None:
        file = self.next_step_file(label, "optional")
        file.write_text(message + "\n", encoding="utf-8")
        self.log(f"SKIP/FAIL optional {label} -> {file}")
        self.optional_failures.append(f"{label} (exit 1): {file}")

    def die(self, message: str) -> None:
        raise SystemExit(message)

    def find_jucewright(self) -> bool:
        if self.jucewright is not None and is_executable(self.jucewright):
            return True

        exe = executable_name("jucewright")
        candidates = [
            self.jucewright_build_dir / "jucewright_cli_artefacts" / exe,
            self.jucewright_build_dir / "jucewright_cli_artefacts" / "Debug" / exe,
            self.jucewright_build_dir / "jucewright_cli_artefacts" / "Release" / exe,
            self.root_dir / "modules" / "jucewright" / "build" / "jucewright_cli_artefacts" / exe,
            self.root_dir / "modules" / "jucewright" / "build" / "jucewright_cli_artefacts" / "Debug" / exe,
            self.root_dir / "modules" / "jucewright" / "build" / "jucewright_cli_artefacts" / "Release" / exe,
        ]

        for candidate in candidates:
            if is_executable(candidate):
                self.jucewright = candidate
                return True

        for search_root in [self.jucewright_build_dir, self.root_dir / "modules" / "jucewright"]:
            if not search_root.exists():
                continue
            for candidate in search_root.rglob(exe):
                if is_executable(candidate):
                    self.jucewright = candidate
                    return True

        return False

    def build_jucewright(self) -> None:
        jobs = str(os.cpu_count() or 4)
        self.log(f"Building jucewright in {self.jucewright_build_dir}")
        subprocess.check_call([
            "cmake",
            "-S",
            str(self.root_dir / "modules" / "jucewright"),
            "-B",
            str(self.jucewright_build_dir),
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DJUCEWRIGHT_BUILD_CLI=ON",
            "-DJUCEWRIGHT_BUILD_TESTS=OFF",
        ], cwd=self.root_dir)
        subprocess.check_call([
            "cmake",
            "--build",
            str(self.jucewright_build_dir),
            "--target",
            "jucewright_cli",
            "--config",
            "Debug",
            "--parallel",
            jobs,
        ], cwd=self.root_dir)

    def projucer_path(self) -> Path:
        if os.environ.get("PROJUCER"):
            return Path(os.environ["PROJUCER"]).expanduser()
        if is_macos():
            return Path.home() / "JUCE" / "Projucer.app" / "Contents" / "MacOS" / "Projucer"
        if is_windows():
            return Path("C:/JUCE/Projucer.exe")
        return Path.home() / "JUCE" / "Projucer"

    def find_msbuild(self) -> str:
        if os.environ.get("MSBUILD"):
            return os.environ["MSBUILD"]

        for name in ["MSBuild.exe", "MSBuild"]:
            path = shutil.which(name)
            if path:
                return path

        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        if vswhere.exists():
            completed = subprocess.run([
                str(vswhere),
                "-latest",
                "-requires",
                "Microsoft.Component.MSBuild",
                "-find",
                r"MSBuild\**\Bin\MSBuild.exe",
            ], text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            for line in completed.stdout.splitlines():
                if line.strip():
                    return line.strip()

        return "MSBuild"

    def build_app(self) -> None:
        projucer = self.projucer_path()
        if not projucer.exists():
            self.die(f"Projucer not found: {projucer}. Set PROJUCER to override.")

        self.log("Resaving osci-render.jucer")
        subprocess.check_call([str(projucer), "--resave", "osci-render.jucer"], cwd=self.root_dir)
        self.log("Building osci-render Debug standalone")

        if is_macos():
            subprocess.check_call([
                "xcodebuild",
                "-project",
                "osci-render.xcodeproj",
                "-scheme",
                "osci-render - Standalone Plugin",
                "-configuration",
                "Debug",
                "-arch",
                "arm64",
                "build",
            ], cwd=self.root_dir / "Builds" / "osci-render" / "MacOSX")
        elif is_windows():
            subprocess.check_call([
                self.find_msbuild(),
                str(self.root_dir / "Builds" / "osci-render" / "VisualStudio2022" / "osci-render.sln"),
                "/p:Configuration=Debug",
                "/p:Platform=x64",
                "/m",
            ], cwd=self.root_dir)
        elif is_linux():
            subprocess.check_call([
                "make",
                "-C",
                str(self.root_dir / "Builds" / "osci-render" / "LinuxMakefile"),
                "CONFIG=Debug",
                "Standalone",
                f"-j{os.cpu_count() or 4}",
            ], cwd=self.root_dir)
        else:
            self.die(f"Unsupported platform for --build-app: {platform.system()}")

    def session_pids(self) -> list[int]:
        if self.jucewright is None or not is_executable(self.jucewright):
            return []

        completed = subprocess.run(self.jw("list"), cwd=self.root_dir, text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if completed.returncode != 0:
            return []

        pids: list[int] = []
        pattern = re.compile(r"^(\S+)\s+pid=(\d+)")
        for line in completed.stdout.splitlines():
            match = pattern.search(line)
            if match and match.group(1) == self.session:
                pids.append(int(match.group(2)))
        return pids

    def terminate_pid(self, pid: int) -> None:
        if is_windows():
            subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return

        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        except PermissionError:
            return

        deadline = time.time() + 2.0
        while time.time() < deadline:
            if not process_exists(pid):
                return
            time.sleep(0.1)

        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass

    def stop_app(self) -> None:
        if self.keep_app:
            return

        pids = []
        if self.app_pid is not None:
            pids.append(self.app_pid)
        pids.extend(pid for pid in self.session_pids() if pid not in pids)

        for pid in pids:
            self.terminate_pid(pid)

        self.app_pid = None

    def launch_app(self, label: str) -> None:
        self.stop_app()
        time.sleep(0.5)

        self.launch_index += 1
        suffix = f"{self.launch_index:02d}_{slug(label)}"
        launch_home = self.automation_home_root / suffix
        launch_home.mkdir(parents=True, exist_ok=True)
        stdout_log = self.artifact_dir / f"osci-render.{suffix}.stdout.log"
        stderr_log = self.artifact_dir / f"osci-render.{suffix}.stderr.log"
        launch_log = self.artifact_dir / f"launch.{suffix}.json"

        self.log(f"Launching osci-render ({label})")
        command = self.jw(
            "launch",
            "--app",
            self.app_path,
            "--app-name",
            "osci-render",
            "--session",
            self.session,
            "--artifact-dir",
            self.artifact_dir,
            "--home",
            launch_home,
            "--source-home",
            self.source_home,
            "--copy-setting",
            "osci-licensing.settings",
            "--stdout",
            stdout_log,
            "--stderr",
            stderr_log,
            "--timeout-ms",
            self.session_timeout_seconds * 1000,
        )

        completed = subprocess.run([str(part) for part in command], cwd=self.root_dir, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        launch_log.write_text(completed.stdout, encoding="utf-8")
        if completed.returncode != 0:
            self.die(f"Timed out waiting for jucewright session '{self.session}' (see {launch_log}, {stderr_log})")

        try:
            payload = json.loads(completed.stdout)
            matched = payload.get("matchedSession", {})
            pid = matched.get("pid")
            self.app_pid = int(pid) if pid is not None else None
        except Exception:
            self.app_pid = None

        time.sleep(0.9)

    def mcp_call(self, label: str, request: dict) -> bool:
        payload = json.dumps(request)
        return self.step_action(label, lambda: self.call(self.jw("mcp"), stdin=payload + "\n"), True, "mcp", ".json")

    def native_escape(self) -> None:
        if is_macos():
            self.call(["osascript", "-e", 'tell application "System Events" to key code 53'])
            return
        if is_windows():
            powershell = shutil.which("powershell") or shutil.which("pwsh")
            if powershell:
                self.call([powershell, "-NoProfile", "-Command", "$w=New-Object -ComObject WScript.Shell; $w.SendKeys('{ESC}')"])
                return
        if is_linux():
            xdotool = shutil.which("xdotool")
            if xdotool:
                self.call([xdotool, "key", "Escape"])
                return
        self.call(self.cli("press", "Escape"))

    def check_png_not_blank(self, file: Path) -> None:
        data = file.read_bytes()
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise StepError("not a png")

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
            return

        raw = zlib.decompress(payload)
        stride = width * channels
        prior = bytearray(stride)
        values: list[int] = []
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
                    raise StepError("unsupported png filter")

            prior = recon
            if color_type in (2, 6):
                values.extend(recon[i] for i in range(0, len(recon), channels))
                values.extend(recon[i] for i in range(1, len(recon), channels))
                values.extend(recon[i] for i in range(2, len(recon), channels))
            else:
                values.extend(recon[0::channels])

        if not values:
            raise StepError("empty png")

        minimum = min(values)
        maximum = max(values)
        mean = sum(values) / len(values)
        print(f"min={minimum} max={maximum} mean={mean:.2f}")
        if maximum < 10 or maximum - minimum < 4:
            raise StepError("image appears blank or nearly flat")

    def node_label(self, node: dict) -> str:
        for key in ["name", "title", "componentName", "componentId", "class"]:
            value = str(node.get(key, "")).strip()
            if value:
                return value
        return str(node.get("ref", "unnamed"))

    def is_visible_control(self, node: dict) -> bool:
        if not node.get("visible", True) or not node.get("enabled", True):
            return False
        if node.get("role") == "ignored" or not node.get("ref"):
            return False
        if SKIP_CONTROL_NAMES.search(self.node_label(node)):
            return False
        bounds = node.get("bounds", {})
        return bounds.get("w", 1) > 0 and bounds.get("h", 1) > 0

    def slider_value(self, node: dict, index: int) -> float | None:
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

    def first_enabled_option_index(self, node: dict):
        options = node.get("options", [])
        if not isinstance(options, list):
            return None

        current_text = str(node.get("value", ""))
        fallback = None
        for option in options:
            if not isinstance(option, dict):
                continue
            if not option.get("enabled", True) or option.get("separator", False) or option.get("sectionHeader", False):
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

    def discover_visible_controls(self, snapshot_file: Path, max_controls: int) -> list[tuple[str, str, str, str, str]]:
        data = json.loads(snapshot_file.read_text(encoding="utf-8"))
        root = data.get("tree", {})
        rows: list[tuple[str, str, str, str, str]] = []

        for node in walk_tree(root):
            if max_controls and len(rows) >= max_controls:
                break
            if not self.is_visible_control(node):
                continue

            role = node.get("role", "")
            actions = node.get("actions", []) or []
            name = self.node_label(node).replace("\t", " ").replace("\n", " ")
            ref = node["ref"]

            if role == "slider":
                value = self.slider_value(node, len(rows))
                if value is not None:
                    rows.append((ref, role, "set-value", f"{value:.8g}", name))
            elif role == "comboBox":
                index = self.first_enabled_option_index(node)
                if index is not None:
                    rows.append((ref, role, "select-index", str(index), name))
            elif role in ["button", "toggleButton", "checkBox"] and node.get("toggleable", False):
                desired = "false" if bool(node.get("checked", node.get("toggleState", False))) else "true"
                rows.append((ref, role, "set-checked", desired, name))
            elif role == "editableText" and not node.get("readOnly", False):
                rows.append((ref, role, "fill", "automation", name))
            elif "set_checked" in actions:
                desired = "false" if bool(node.get("checked", False)) else "true"
                rows.append((ref, role, "set-checked", desired, name))

        return rows

    def exercise_visible_controls(self, label: str, max_controls: int, *locator_args: object) -> None:
        self.step += 1
        snapshot_file = self.artifact_dir / f"{self.step:03d}_dynamic_{slug(label)}_snapshot.json"
        controls_file = self.artifact_dir / f"{self.step:03d}_dynamic_{slug(label)}_controls.tsv"

        self.log(f"DISCOVER visible controls in {label}")
        if not self.call_to_file(self.cli("snapshot", "--json", "--full", "--depth", "18", *locator_args), snapshot_file, snapshot_file.with_suffix(snapshot_file.suffix + ".stderr")):
            self.log(f"SKIP/FAIL optional dynamic snapshot {label} -> {snapshot_file}.stderr")
            self.optional_failures.append(f"dynamic snapshot {label} (exit 1): {snapshot_file}.stderr")
            return

        try:
            rows = self.discover_visible_controls(snapshot_file, max_controls)
            controls_file.write_text("".join("\t".join(row) + "\n" for row in rows), encoding="utf-8")
        except Exception as exc:
            controls_file.with_suffix(controls_file.suffix + ".stderr").write_text(str(exc) + "\n", encoding="utf-8")
            self.log(f"SKIP/FAIL optional dynamic discovery {label} -> {controls_file}.stderr")
            self.optional_failures.append(f"dynamic discovery {label} (exit 1): {controls_file}.stderr")
            return

        name_filter = os.environ.get("DYNAMIC_NAME_FILTER", "").lower()
        if name_filter:
            rows = [row for row in rows if name_filter in row[4].lower()]

        if not rows:
            self.log(f"INFO no visible controls discovered for {label} -> {controls_file}")
            return

        for ref, _role, action, value, name in rows:
            if action == "set-value":
                self.try_step(f"dynamic {label} set slider {name}", self.cli("set-value", ref, "--timeout-ms", "3000", value))
            elif action == "select-index":
                self.try_step(f"dynamic {label} select option {name}", self.cli("select-option", ref, "--index", value, "--timeout-ms", "3000"))
            elif action == "set-checked":
                self.try_step(f"dynamic {label} set checked {name}", self.cli("set-checked", ref, "--timeout-ms", "3000", value))
            elif action == "fill":
                self.try_step(f"dynamic {label} fill {name}", self.cli("fill", ref, "--timeout-ms", "3000", value))

    def verify_new_recording_file(self, marker_file: Path) -> None:
        marker_time = marker_file.stat().st_mtime
        extensions = {".mp4", ".mov", ".wav", ".m4v"}
        candidates: list[tuple[float, int, Path]] = []

        for root, _dirs, files in os.walk(self.recording_dir):
            for name in files:
                path = Path(root) / name
                if path.suffix.lower() not in extensions:
                    continue
                stat = path.stat()
                if stat.st_mtime >= marker_time and stat.st_size > 512:
                    candidates.append((stat.st_mtime, stat.st_size, path))

        if not candidates:
            raise StepError(f"no recording file larger than 512 bytes was created in {self.recording_dir}")

        for _mtime, size, path in sorted(candidates, reverse=True)[:3]:
            print(f"{path}\t{size} bytes")

    def component_size(self, label: str, *locator_args: object) -> tuple[int, int]:
        snapshot_file = self.artifact_dir / f"geometry_{slug(label)}.json"
        if not self.call_to_file(self.cli("snapshot", "--json", "--interesting", "--depth", "4", *locator_args), snapshot_file, snapshot_file.with_suffix(snapshot_file.suffix + ".stderr")):
            raise StepError(f"geometry snapshot failed for {label}")

        data = json.loads(snapshot_file.read_text(encoding="utf-8"))
        for node in walk_tree(data.get("tree", {})):
            bounds = node.get("bounds", {})
            width = int(round(float(bounds.get("w", 0))))
            height = int(round(float(bounds.get("h", 0))))
            if width > 0 and height > 0:
                return width, height

        raise StepError("component has no usable bounds")

    def drag_component_percent(self, label: str, x_percent: int, y_percent: int, dx: int, dy: int, *locator_args: object) -> None:
        try:
            width, height = self.component_size(label, *locator_args)
        except Exception as exc:
            self.optional_failure(f"{label} geometry unavailable", str(exc))
            return

        x = max(1, min(width - 1, width * x_percent // 100))
        y = max(1, min(height - 1, height * y_percent // 100))
        self.try_step(label, self.cli("drag", *locator_args, "--position", f"{x},{y}", "--dx", dx, "--dy", dy, "--steps", "10", "--timeout-ms", "5000"))

    def open_examples_panel(self) -> None:
        self.try_step("disable external audio before opening examples", self.cli("set-checked", "--component-name", "inputEnabled", "--exact", "--timeout-ms", "3000", "false"))
        self.try_step("wait after disabling external audio before examples", self.cli("wait", "--ms", "250"))
        self.run_step("open files and examples panel", self.cli("click", "--name", "openFiles", "--exact", "--timeout-ms", "5000"))
        self.run_step("examples panel is open", self.cli("locator", "--class", "OpenFileComponent", "--timeout-ms", "5000"))
        self.try_step("examples panel snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "10"))

    def load_example(self, name: str, expect_source: bool = False) -> None:
        nth = EXAMPLE_INDEX.get(name)
        if nth is None:
            self.die(f"No example index mapping for '{name}'")

        self.open_examples_panel()
        self.run_step(f"load example {name}", self.cli("click", "--class", "osci::GridItemComponent", "--nth", nth, "--force", "--timeout-ms", "6000"))
        self.run_step(f"wait after loading {name}", self.cli("wait", "--ms", "400"))
        self.run_step(f"snapshot after loading {name}", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        if expect_source:
            self.try_step(f"source editor for {name}", self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "ErrorCodeEditorComponent", "--nth", "0"))
        self.try_step(f"visualiser screenshot after {name}", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / f"visualiser_{slug(name)}.png"))
        self.try_step(f"dismiss transient overlays after {name}", self.cli("press", "Escape"))

    def exercise_current_file_common(self, label: str) -> None:
        self.try_step(f"describe file controls for {label}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "FileControlsComponent"))
        self.try_step(f"describe quick controls for {label}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "QuickControlsBar"))
        self.try_step(f"randomise effects for {label}", self.cli("click", "--name", "randomise", "--exact", "--timeout-ms", "5000"))
        self.try_step(f"toggle auto-link lfos for {label}", self.cli("click", "--name", "autoLink", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"restore auto-link lfos for {label}", self.cli("click", "--name", "autoLink", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"visualiser screenshot for {label}", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / f"visualiser_common_{slug(label)}.png"))
        self.exercise_visible_controls(f"quick controls for {label}", 0, "--class", "QuickControlsBar")

    def exercise_text_file_controls(self, name: str) -> None:
        self.try_step(f"text font describe for {name}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "TxtComponent", "--nth", "0"))
        self.try_step(f"select text font for {name}", self.cli("select-option", "--class", "TxtComponent", "--nth", "0", "--index", "0", "--timeout-ms", "3000"))

    def exercise_lua_file_controls(self, name: str) -> None:
        self.try_step(f"lua source editor for {name}", self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "ErrorCodeEditorComponent", "--nth", "0"))
        self.try_step(f"reset lua state for {name}", self.cli("click", "--name", "luaReset", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"open lua scripting reference for {name}", self.cli("click", "--name", "luaHelp", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"lua scripting reference snapshot for {name}", self.cli("snapshot", "--json", "--interesting", "--depth", "10"))
        self.try_step(f"close lua scripting reference for {name}", self.cli("click", "--name", "closeOverlay", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"lua slider a for {name}", self.cli("set-value", "--role", "slider", "--name", "Lua Slider A", "--exact", "--timeout-ms", "3000", "0.25"))
        self.try_step(f"lua slider b for {name}", self.cli("set-value", "--role", "slider", "--name", "Lua Slider B", "--exact", "--timeout-ms", "3000", "0.75"))
        self.try_step(f"pause lua console for {name}", self.cli("click", "--name", "pauseConsole", "--exact", "--timeout-ms", "3000"))
        self.try_step(f"clear lua console for {name}", self.cli("click", "--name", "clearConsole", "--exact", "--timeout-ms", "3000"))

    def exercise_fractal_file_controls(self, name: str) -> None:
        self.try_step(f"fractal editor describe for {name}", self.cli("describe", "--json", "--interesting", "--depth", "10", "--class", "FractalComponent"))
        self.exercise_visible_controls(f"fractal editor for {name}", 0, "--class", "FractalComponent")
        self.try_step(f"add fractal rule for {name}", self.cli("click", "--name", "+ Add Rule", "--exact", "--timeout-ms", "3000"))

    def exercise_frame_and_timeline_controls(self, label: str, kind: str) -> None:
        if kind in ["gpla", "gif", "mp4", "mov"]:
            self.try_step(f"frame settings describe for {label}", self.cli("describe", "--json", "--interesting", "--depth", "10", "--name", "Frame settings", "--exact"))
            self.try_step(f"set frames per second for {label}", self.cli("fill", "--role", "editableText", "--name", "Frames per second", "--exact", "--timeout-ms", "3000", "12.00"))

        if kind in ["gif", "png", "jpg", "mp4", "mov"]:
            self.try_step(f"toggle invert image for {label}", self.cli("click", "--role", "button", "--name", "Invert Image", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"set image threshold for {label}", self.cli("set-value", "--role", "slider", "--name", "Image Threshold", "--exact", "--timeout-ms", "3000", "0.4"))
            self.try_step(f"set image stride for {label}", self.cli("set-value", "--role", "slider", "--name", "Image Stride", "--exact", "--timeout-ms", "3000", "3"))

        if kind in ["gpla", "gif", "flac", "mp4", "mov"]:
            self.try_step(f"timeline describe for {label}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "TimelineComponent"))
            self.try_step(f"timeline pause for {label}", self.cli("click", "--name", "Pause", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"timeline play for {label}", self.cli("click", "--name", "Play", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"timeline repeat for {label}", self.cli("click", "--name", "Repeat", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"timeline pause after play for {label}", self.cli("click", "--name", "Pause", "--exact", "--timeout-ms", "3000"))
            self.try_step(f"timeline stop for {label}", self.cli("click", "--name", "Stop", "--exact", "--timeout-ms", "3000"))

    def exercise_file_switching(self) -> None:
        self.try_step("switch to previous file", self.cli("click", "--name", "leftArrow", "--exact", "--timeout-ms", "3000"))
        self.try_step("snapshot after previous file", self.cli("snapshot", "--json", "--interesting", "--depth", "10"))
        self.try_step("switch to next file", self.cli("click", "--name", "rightArrow", "--exact", "--timeout-ms", "3000"))
        self.try_step("snapshot after next file", self.cli("snapshot", "--json", "--interesting", "--depth", "10"))

    def exercise_midi_keyboard_clicks(self) -> None:
        self.run_step("midi keyboard snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--timeout-ms", "5000"))
        self.run_step("midi keyboard low note click", self.cli("click", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--position", "16,24", "--force", "--timeout-ms", "3000"))
        self.run_step("midi keyboard middle note click", self.cli("click", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--position", "72,24", "--force", "--timeout-ms", "3000"))
        self.run_step("midi keyboard high note click", self.cli("click", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--position", "140,24", "--force", "--timeout-ms", "3000"))
        self.run_step("midi keyboard multi-note drag", self.cli("drag", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--position", "32,24", "--dx", "180", "--dy", "0", "--steps", "14", "--force", "--timeout-ms", "5000"))
        self.try_step("visualiser screenshot after midi key clicks", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / "visualiser_after_midi_keyboard.png"))

    def select_menu_item(self, item: str) -> list[str]:
        return self.cli("select-option", "--role", "menuBar", "--class", "MenuBarComponent", "--text", item, "--timeout-ms", "5000")

    def ensure_midi_keyboard_visible(self) -> None:
        file = self.next_step_file("midi keyboard visible", "probe")
        self.log("PROBE MIDI keyboard visible")
        if self.call_to_file(self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "CustomMidiKeyboardComponent", "--nth", "0", "--timeout-ms", "2000"), file):
            self.log(f"OK  MIDI keyboard already visible -> {file}")
            return

        self.log(f"INFO MIDI keyboard hidden; enabling Interface > Show MIDI Keyboard -> {file}")
        self.run_step("show midi keyboard preference", self.select_menu_item("Show MIDI Keyboard"))
        self.run_step("wait after showing midi keyboard preference", self.cli("wait", "--ms", "500"))

    def checked_switch_ref_for_component(self, component_name: str) -> tuple[str, bool]:
        snapshot_file = self.artifact_dir / f"switch_{slug(component_name)}.json"
        if not self.call_to_file(self.cli("snapshot", "--json", "--interesting", "--depth", "6", "--component-name", component_name, "--timeout-ms", "3000"), snapshot_file, snapshot_file.with_suffix(snapshot_file.suffix + ".stderr")):
            raise StepError(f"could not snapshot switch component {component_name}")

        data = json.loads(snapshot_file.read_text(encoding="utf-8"))
        for node in walk_tree(data.get("tree", {})):
            actions = node.get("actions", []) or []
            if "set_checked" in actions:
                return str(node.get("ref", "")), bool(node.get("checked"))

        raise StepError("no checked switch descendant")

    def ensure_checked_switch(self, component_name: str, label: str, desired: bool = True) -> None:
        try:
            ref, checked = self.checked_switch_ref_for_component(component_name)
        except Exception:
            self.run_step(f"{label} switch lookup", lambda: (_ for _ in ()).throw(StepError("switch lookup failed")))
            return

        if checked == desired:
            self.try_step(f"{label} already {bool_text(desired)}", self.cli("wait", "--ms", "1"))
            return

        self.run_step(label, self.cli("set-checked", ref, "--timeout-ms", "3000", bool_text(desired)))

    def exercise_modulation_graph_handles_for_tab(self, tab: str) -> None:
        if tab.startswith("RAND"):
            self.run_step(f"random graph drag for {tab}", self.cli("drag", "--class", "RandomGraphComponent", "--nth", "0", "--position", "80,24", "--dx", "50", "--dy", "0", "--steps", "8", "--timeout-ms", "5000"))
            return

        if tab == "INPUT":
            self.try_step("input graph drag first handle", self.cli("drag", "--class", "NodeGraphComponent", "--nth", "2", "--position", "18,90", "--dx", "20", "--dy", "-20", "--steps", "8", "--timeout-ms", "5000", "--force"))
            self.try_step("input graph drag second handle", self.cli("drag", "--class", "NodeGraphComponent", "--nth", "2", "--position", "105,44", "--dx", "-14", "--dy", "18", "--steps", "8", "--timeout-ms", "5000", "--force"))
            return

        for graph_nth in [0, 1, 2]:
            self.drag_component_percent(f"drag first graph handle for {tab} graph {graph_nth}", 8, 65, 24, -18, "--class", "NodeGraphComponent", "--nth", graph_nth)
            self.drag_component_percent(f"drag second graph handle for {tab} graph {graph_nth}", 50, 35, -12, 20, "--class", "NodeGraphComponent", "--nth", graph_nth)
            self.drag_component_percent(f"drag curve graph handle for {tab} graph {graph_nth}", 35, 45, 0, 24, "--class", "NodeGraphComponent", "--nth", graph_nth)

    def mod_drag_description(self, tab: str) -> str:
        if tab.startswith("LFO "):
            return f"MOD:LFO:{int(tab.split()[1]) - 1}"
        if tab.startswith("ENV "):
            return f"MOD:ENV:{int(tab.split()[1]) - 1}"
        if tab.startswith("RAND "):
            return f"MOD:RNG:{int(tab.split()[1]) - 1}"
        if tab == "INPUT":
            return "MOD:SC:0"
        raise StepError(f"unknown modulation tab {tab}")

    def exercise_modulation_source_assignment(self, tab: str) -> None:
        handle_nth = MOD_HANDLE_INDEX.get(tab)
        if handle_nth is None:
            self.die(f"No modulation handle index mapping for '{tab}'")

        drag_description = self.mod_drag_description(tab)
        self.run_step(f"select modulation tab for assignment {tab}", self.cli("click", "--class", "ModTabHandle", "--nth", handle_nth, "--force", "--timeout-ms", "3000"))
        self.run_step(f"drag modulation source {tab} to perspective", self.cli("drag-to", "--class", "ModTabHandle", "--nth", handle_nth, "--target-class", "KnobContainerComponent", "--target-nth", "1", "--force", "--steps", "16", "--timeout-ms", "6000"))
        self.run_step(f"drop modulation source {tab} on perspective", self.cli("drop", "--class", "KnobContainerComponent", "--nth", "1", "--description", drag_description, "--force", "--timeout-ms", "5000"))
        self.run_step(f"wait after modulation drop {tab}", self.cli("wait", "--ms", "250"))
        self.run_step(f"snapshot modulation assignment {tab}", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "ModTabHandle", "--nth", handle_nth))
        self.run_step(f"drag modulation strength {tab}", self.cli("drag", "--class", "DepthIndicator", "--nth", "0", "--dx", "0", "--dy", "-18", "--steps", "8", "--force", "--timeout-ms", "5000"))
        self.run_step(f"right click modulation strength {tab} and set bipolar", self.cli("right-click", "--class", "DepthIndicator", "--nth", "0", "--force", "--menu-item", "Make Bipolar", "--timeout-ms", "5000"))
        self.try_step(f"dismiss modulation context menu {tab}", self.cli("press", "Escape"))

    def exercise_hello_world_editor(self) -> None:
        edited_text = "hello\njucewright automation\nworld"
        self.run_step("edit Hello World source text", self.cli("fill", "--class", "ErrorCodeEditorComponent", "--nth", "0", "--timeout-ms", "5000", edited_text))
        self.run_step("wait after editing Hello World source text", self.cli("wait", "--ms", "500"))
        self.run_step("snapshot after editing Hello World source text", self.cli("snapshot", "--json", "--interesting", "--depth", "8", "--class", "ErrorCodeEditorComponent", "--nth", "0"))
        self.run_step("collapse Hello World source editor", self.cli("click", "--role", "button", "--name", "Collapse", "--exact", "--timeout-ms", "3000"))
        self.run_step("snapshot after collapsing Hello World source editor", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))

    def try_open_effect_browser(self, effect: str) -> None:
        file = self.next_step_file(f"ensure effect browser for {effect}")
        self.log(f"TRY open effect browser for {effect}")
        if self.call_to_file(self.cli("click", "--name", "Add new effect", "--exact", "--timeout-ms", "3000"), file):
            self.log(f"OK  open effect browser for {effect} -> {file}")
        else:
            self.log(f"INFO effect browser for {effect} was already open or will be verified by the next required step -> {file}")

    def exercise_effect(self, effect: str) -> None:
        nth = EFFECT_INDEX.get(effect)
        if nth is None:
            self.die(f"No effect index mapping for '{effect}'")

        self.try_open_effect_browser(effect)
        self.run_step(f"find effect browser item {effect}", self.cli("locator", "--class", "osci::GridItemComponent", "--nth", nth, "--timeout-ms", "5000"))
        self.run_step(f"add effect {effect}", self.cli("click", "--class", "osci::GridItemComponent", "--nth", nth, "--force", "--timeout-ms", "6000"))
        self.try_step(f"wait after adding effect {effect}", self.cli("wait", "--ms", "200"))
        self.try_step(f"snapshot selected effect {effect}", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "DraggableListBox"))
        self.exercise_visible_controls(f"selected effect {effect}", 18, "--class", "DraggableListBox")

    def exercise_menu(self, menu: str) -> None:
        self.try_step(f"open menu {menu}", self.cli("click", "--role", "menuItem", "--name", menu, "--exact", "--timeout-ms", "3000"))
        self.try_step(f"snapshot menu {menu}", self.cli("snapshot", "--json", "--full", "--depth", "8"))
        self.try_step(f"close menu {menu}", self.cli("press", "Escape"))

    def close_overlay(self) -> None:
        self.call(self.cli("click", "--role", "button", "--name", "closeOverlay", "--exact", "--timeout-ms", "3000"))
        self.call(self.cli("wait", "--ms", "250"))

    def exercise_about_dialog(self) -> None:
        self.run_step("open about dialog", self.select_menu_item("About osci-render"))
        self.run_step("about dialog snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.try_step("about website button trial", self.cli("click", "--name", "Website", "--exact", "--trial", "--timeout-ms", "3000"))
        self.try_step("about report issue button trial", self.cli("click", "--name", "Report Issue", "--exact", "--trial", "--timeout-ms", "3000"))
        self.run_step("close about dialog", self.close_overlay)

    def exercise_license_dialog(self) -> None:
        self.try_step("open license and updates dialog", self.select_menu_item("License and Updates..."))
        self.try_step("license and updates snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.try_step("license and updates full snapshot", self.cli("snapshot", "--json", "--full", "--depth", "14"))
        self.try_step("close license and updates dialog", self.close_overlay)

    def exercise_recording_dialog(self) -> None:
        self.run_step("open recording settings dialog", self.select_menu_item("Recording Settings..."))
        self.run_step("recording settings snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.try_step("set recording video quality", self.cli("set-value", "--role", "slider", "--name", "Video Quality", "--exact", "--timeout-ms", "3000", "0.62"))
        self.try_step("set recording resolution", self.cli("set-value", "--role", "slider", "--name", "Resolution", "--exact", "--timeout-ms", "3000", "720"))
        self.try_step("set recording frame rate", self.cli("set-value", "--role", "slider", "--name", "Frame Rate", "--exact", "--timeout-ms", "3000", "30"))
        self.try_step("toggle lossless video", self.cli("click", "--role", "button", "--name", "Lossless Video", "--exact", "--timeout-ms", "3000"))
        self.try_step("select compression speed medium", self.cli("select-option", "--role", "comboBox", "--name", "Compression speed", "--exact", "--text", "medium", "--timeout-ms", "3000"))
        self.try_step("select video codec h265", self.cli("select-option", "--role", "comboBox", "--name", "Video codec", "--exact", "--text", "H.265/HEVC", "--timeout-ms", "3000"))
        self.try_step("set custom shared texture output name", self.cli("fill", "--role", "editableText", "--name", "Custom Syphon/Spout name", "--exact", "--timeout-ms", "3000", "osci-render-automation"))
        self.exercise_visible_controls("recording settings dialog", 0, "--class", "RecordingSettingsOverlay")
        self.run_step("close recording settings dialog", self.close_overlay)

    def exercise_audio_dialog(self) -> None:
        self.run_step("open audio settings dialog", self.select_menu_item("Settings..."))
        self.run_step("wait for audio settings overlay animation", self.cli("wait", "--ms", "1200"))
        self.run_step("audio settings snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "14"))
        self.try_step("audio settings full snapshot", self.cli("snapshot", "--json", "--full", "--depth", "16"))
        self.exercise_visible_controls("audio settings dialog", 0, "--class", "AudioSettingsOverlay")
        self.run_step("close audio settings dialog", self.close_overlay)

    def exercise_visualiser_settings_dialog(self) -> None:
        self.run_step("open visualiser settings dialog", self.cli("click", "--name", "settings", "--timeout-ms", "3000"))
        self.run_step("visualiser settings snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.exercise_visible_controls("visualiser settings dialog", 0, "--role", "dialogWindow", "--name", "Visualiser Settings", "--exact")
        self.run_step("close visualiser settings dialog", self.cli("press", "Escape", "--role", "dialogWindow", "--name", "Visualiser Settings", "--exact", "--timeout-ms", "3000"))

    def exercise_application_dialogs(self) -> None:
        self.exercise_about_dialog()
        self.exercise_license_dialog()
        self.exercise_recording_dialog()
        self.exercise_audio_dialog()
        self.exercise_visualiser_settings_dialog()
        self.try_step("syphon spout input dialog", self.select_menu_item("Select Syphon/Spout Input..."))
        self.try_step("syphon spout input snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        self.exercise_visible_controls("syphon spout input dialog", 0, "--role", "dialogWindow", "--name", "Select Syphon/Spout Input", "--exact")
        self.try_step("close syphon spout input dialog", self.close_overlay)

    def configure_visualiser_recording_for_automation(self) -> None:
        self.run_step("open recording settings for visualiser recording", self.select_menu_item("Recording Settings..."))
        self.try_step("recording uses h264", self.cli("select-option", "--role", "comboBox", "--name", "Video codec", "--text", "H.264", "--timeout-ms", "3000"))
        self.try_step("recording uses ultrafast compression", self.cli("select-option", "--role", "comboBox", "--name", "Compression speed", "--text", "ultrafast", "--timeout-ms", "3000"))
        self.try_step("recording disables lossless video", self.cli("set-checked", "--role", "button", "--name", "Lossless Video", "--exact", "--timeout-ms", "3000", "false"))
        self.try_step("recording enables video", self.cli("set-checked", "--role", "button", "--name", "Record Video", "--exact", "--timeout-ms", "3000", "true"))
        self.try_step("recording disables audio mux", self.cli("set-checked", "--role", "button", "--name", "Record Audio", "--exact", "--timeout-ms", "3000", "false"))
        self.try_step("recording resolution low", self.cli("set-value", "--role", "slider", "--name", "Resolution", "--exact", "--timeout-ms", "3000", "256"))
        self.try_step("recording frame rate low", self.cli("set-value", "--role", "slider", "--name", "Frame Rate", "--exact", "--timeout-ms", "3000", "10"))
        self.try_step("recording video quality medium", self.cli("set-value", "--role", "slider", "--name", "Video Quality", "--exact", "--timeout-ms", "3000", "0.45"))
        self.run_step("close recording settings for visualiser recording", self.close_overlay)

    def exercise_visualiser_recording(self) -> None:
        marker_file = self.artifact_dir / "visualiser-recording-start.marker"

        if not self.include_native:
            self.try_step("visualiser recording skipped because stopping opens a native save dialog", lambda: print("skipped"))
            return

        self.configure_visualiser_recording_for_automation()
        marker_file.write_text("", encoding="utf-8")
        self.run_step("start visualiser recording", self.cli("click", "--name", "Record", "--exact", "--timeout-ms", "5000"))
        self.run_step("wait while recording visualiser", self.cli("wait", "--ms", "2600"))
        self.try_step("visualiser screenshot during recording", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / "visualiser_recording_active.png"))
        self.run_step("stop visualiser recording", self.cli("click", "--name", "Record", "--exact", "--timeout-ms", "5000"))
        self.try_step("cancel native recording save dialog", self.native_escape)
        self.run_step("wait after cancelling visualiser recording save dialog", self.cli("wait", "--ms", "1000"))
        self.try_step("verify visualiser recording file if native dialog saved", lambda: self.verify_new_recording_file(marker_file))

    def exercise_native_dialog_entries(self) -> None:
        self.try_step("render audio file to video native dialog trial", self.select_menu_item("Render Audio File to Video..."))
        self.try_step("close native dialog with escape", self.cli("press", "Escape"))

    def prepare_external_fixtures(self) -> None:
        self.fixture_dir.mkdir(parents=True, exist_ok=True)
        fixtures = {
            "automation.gpla": self.root_dir / "Resources" / "gpla" / "fallback.gpla",
            "automation.gif": self.root_dir / "images" / "demo1.gif",
            "automation.png": self.root_dir / "Resources" / "oscilloscope" / "real.png",
            "automation.jpg": self.root_dir / "Resources" / "oscilloscope" / "empty.jpg",
            "automation.flac": self.root_dir / "Resources" / "audio" / "sosci.flac",
            "automation.mp4": self.root_dir / "osci-render-website" / "assets" / "images" / "cubes.mp4",
            "automation.mov": self.root_dir / "osci-render-website" / "assets" / "images" / "cubes.mp4",
        }
        for name, source in fixtures.items():
            shutil.copyfile(source, self.fixture_dir / name)

    def exercise_external_file(self, kind: str, path: Path) -> None:
        self.launch_app(f"external {kind}")
        self.run_step(f"drop external {kind} file", self.cli("drop-files", "--file", path, "--timeout-ms", "5000"))
        self.run_step(f"wait after dropping external {kind}", self.cli("wait", "--ms", "750"))
        self.run_step(f"snapshot external {kind}", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
        screenshot = self.artifact_dir / f"visualiser_external_{slug(kind)}.png"
        self.run_step(f"visualiser screenshot external {kind}", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", screenshot))
        self.try_step(f"visualiser nonblank external {kind}", lambda: self.check_png_not_blank(screenshot))
        self.exercise_current_file_common(f"external {kind}")
        self.exercise_frame_and_timeline_controls(f"external {kind}", kind)

    def exercise_external_file_type_passes(self) -> None:
        self.prepare_external_fixtures()
        external_types = ["gpla", "gif", "png", "jpg", "flac", "mp4", "mov"]
        if self.quick:
            external_types = ["gpla", "gif", "flac"]

        for kind in external_types:
            self.exercise_external_file(kind, self.fixture_dir / f"automation.{kind}")

    def write_summary(self) -> None:
        summary = {
            "session": self.session,
            "artifactDir": str(self.artifact_dir),
            "quick": self.quick,
            "failures": len(self.failures),
            "optionalFailures": len(self.optional_failures),
            "platform": platform.system(),
        }
        (self.artifact_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

        if self.optional_failures:
            self.log("Optional failures:")
            text = "".join(f"  - {failure}\n" for failure in self.optional_failures)
            print(text, end="")
            (self.artifact_dir / "optional-failures.txt").write_text(text, encoding="utf-8")

        if self.failures:
            self.log("Failures:")
            text = "".join(f"  - {failure}\n" for failure in self.failures)
            print(text, end="")
            (self.artifact_dir / "failures.txt").write_text(text, encoding="utf-8")
            self.log(f"Summary: {self.artifact_dir / 'summary.json'}")
        else:
            self.log(f"Completed without required failures. Summary: {self.artifact_dir / 'summary.json'}")

    def run(self) -> int:
        try:
            if not self.find_jucewright():
                self.build_jucewright()
                if not self.find_jucewright():
                    self.die("Could not find jucewright after build")

            if self.build_app_requested or not is_executable(self.app_executable):
                self.build_app()

            if not is_executable(self.app_executable):
                self.die(f"App executable not found: {self.app_executable}")
            if self.jucewright is None or not is_executable(self.jucewright):
                self.die(f"jucewright not executable: {self.jucewright}")

            self.log(f"Artifacts: {self.artifact_dir}")
            self.log(f"jucewright: {self.jucewright}")
            self.log(f"App: {self.app_executable}")

            self.launch_app("clean startup")

            self.run_step("list sessions", self.jw("list"))
            self.run_step("capabilities", self.cli("capabilities"))
            self.run_step("windows", self.cli("windows"))
            self.run_step("start trace", self.cli("trace-start", "--file", self.artifact_dir / "trace.json"))

            self.run_step("root full snapshot", self.cli("snapshot", "--json", "--full", "--depth", "14"))
            self.run_step("root interesting snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
            self.run_step("root screenshot", self.cli("screenshot", "--target", "root", "--source", "auto", "--file", self.artifact_dir / "root.png"))
            self.run_step("visualiser count", self.cli("count", "--class", "VisualiserComponent", "--nth", "0"))
            self.run_step("visualiser describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "VisualiserComponent", "--nth", "0"))
            self.run_step("visualiser screenshot", self.cli("screenshot", "--class", "VisualiserComponent", "--nth", "0", "--source", "auto", "--file", self.artifact_dir / "visualiser.png"))
            self.run_step("visualiser screenshot nonblank check", lambda: self.check_png_not_blank(self.artifact_dir / "visualiser.png"))

            for menu in ["File", "Edit", "About", "Video", "Audio", "Interface"]:
                self.exercise_menu(menu)

            self.exercise_application_dialogs()

            self.run_step("file controls describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "FileControlsComponent"))
            self.run_step("quick controls describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "QuickControlsBar"))
            self.run_step("volume controls describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "VolumeComponent"))
            self.exercise_visible_controls("quick controls root", 0, "--class", "QuickControlsBar")
            self.exercise_visible_controls("volume controls root", 0, "--class", "VolumeComponent")
            self.exercise_visible_controls("main workspace controls", 25 if self.quick else 120)

            text_examples = ["Hello World"] if self.quick else TEXT_EXAMPLES
            lua_examples = ["Spiral", "Shape Generator"] if self.quick else LUA_EXAMPLES
            model_examples = ["Cube"] if self.quick else MODEL_EXAMPLES
            svg_examples = ["Air Horn"] if self.quick else SVG_EXAMPLES
            fractal_examples = ["Koch Snowflake"] if self.quick else FRACTAL_EXAMPLES

            for example in model_examples:
                self.load_example(example)
                self.exercise_current_file_common(f"model {example}")

            for example in lua_examples:
                self.load_example(example, True)
                self.exercise_current_file_common(f"lua {example}")
                self.exercise_lua_file_controls(example)

            for example in svg_examples:
                self.load_example(example)
                self.exercise_current_file_common(f"svg {example}")

            for example in fractal_examples:
                self.load_example(example)
                self.exercise_current_file_common(f"fractal {example}")
                self.exercise_fractal_file_controls(example)

            for example in text_examples:
                self.load_example(example, True)
                self.exercise_current_file_common(f"text {example}")
                self.exercise_text_file_controls(example)
                if example == "Hello World":
                    self.exercise_hello_world_editor()

            self.exercise_file_switching()

            self.try_step("hidden timeline describe without animatable file", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "TimelineComponent", "--hidden"))

            self.run_step("midi controls describe", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "MidiComponent"))
            self.ensure_checked_switch("midi", "enable midi mode", True)
            self.run_step("wait after enabling midi mode", self.cli("wait", "--ms", "500"))
            self.run_step("midi controls after toggle", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "MidiComponent"))
            self.ensure_midi_keyboard_visible()
            self.exercise_midi_keyboard_clicks()

            self.run_step("modulation tabs snapshot", self.cli("locator", "--format", "json", "--class", "ModTabHandle"))
            mod_tabs = ["LFO 1", "RAND 1", "INPUT", "ENV 1"] if self.quick else MOD_TABS
            for tab in mod_tabs:
                handle_nth = MOD_HANDLE_INDEX.get(tab)
                if handle_nth is None:
                    self.try_step(f"skip unavailable modulation tab {tab}", self.cli("wait", "--ms", "1"))
                    continue
                self.try_step(f"select modulation tab {tab}", self.cli("click", "--class", "ModTabHandle", "--nth", handle_nth, "--force", "--timeout-ms", "2500"))
                self.try_step(f"describe modulation tab {tab}", self.cli("describe", "--json", "--interesting", "--depth", "8", "--class", "ModTabHandle", "--nth", handle_nth))
                self.exercise_modulation_graph_handles_for_tab(tab)
                self.exercise_modulation_source_assignment(tab)

            self.try_step("lfo graph drag", self.cli("drag", "--class", "NodeGraphComponent", "--nth", "0", "--dx", "20", "--dy", "-20", "--steps", "8", "--timeout-ms", "3000"))
            self.try_step("modulation graph snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "NodeGraphComponent", "--nth", "0"))

            self.run_step("stop startup trace before clean effects sweep", self.cli("trace-stop"))
            self.launch_app("clean effects sweep")
            self.run_step("start effects trace", self.cli("trace-start", "--file", self.artifact_dir / "effects-trace.json"))
            self.run_step("audio effects describe", self.cli("describe", "--json", "--interesting", "--depth", "10", "--class", "EffectsComponent"))
            self.try_open_effect_browser("initial effects sweep")
            self.run_step("effect browser snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "10", "--class", "EffectTypeGridComponent"))

            effects = ["Bit Crush", "Rotate", "Scale"] if self.quick else EFFECTS
            for effect in effects:
                self.exercise_effect(effect)

            self.try_step("visualiser settings button", self.cli("click", "--name", "settings", "--timeout-ms", "3000"))
            self.try_step("visualiser settings snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
            self.try_step("close visualiser settings", self.cli("press", "Escape"))
            self.exercise_visualiser_recording()
            self.try_step("shared texture output describe", self.cli("describe", "--json", "--interesting", "--depth", "6", "--name", "Syphon/Spout"))
            self.try_step("open visualiser window trial", self.cli("click", "--name", "new window", "--trial", "--timeout-ms", "3000"))
            self.try_step("fullscreen trial", self.cli("click", "--name", "fullscreen", "--trial", "--timeout-ms", "3000"))

            if self.include_native:
                self.try_step("native open project trial", self.cli("click", "--text", "Open Project", "--exact", "--trial", "--timeout-ms", "3000"))
                self.try_step("native save project as trial", self.cli("click", "--text", "Save Project As", "--exact", "--trial", "--timeout-ms", "3000"))
                self.exercise_native_dialog_entries()

            self.run_step("stop effects trace before external file passes", self.cli("trace-stop"))
            self.exercise_external_file_type_passes()
            self.run_step("start final trace", self.cli("trace-start", "--file", self.artifact_dir / "final-trace.json"))

            self.mcp_call("initialize", {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "osci-render-browser-script", "version": "1.0.0"}}})
            self.mcp_call("tools list", {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
            self.mcp_call("list sessions", {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "juce_list_sessions", "arguments": {}}})
            self.mcp_call("snapshot", {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "juce_snapshot", "arguments": {"session": self.session, "mode": "interesting", "depth": 10, "format": "json"}}})
            self.mcp_call("visualiser screenshot", {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "juce_screenshot", "arguments": {"session": self.session, "locator": {"class": "VisualiserComponent", "nth": 0}, "file": str(self.artifact_dir / "mcp_visualiser.png"), "includeBase64": False}}})

            self.run_step("stop trace", self.cli("trace-stop"))
            self.run_step("final root snapshot", self.cli("snapshot", "--json", "--interesting", "--depth", "12"))
            return 1 if self.failures else 0
        finally:
            self.write_summary()
            self.stop_app()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Browse osci-render exhaustively with Jucewright.")
    parser.add_argument("--build-app", action="store_true", help="Resave the Projucer project and build the Debug standalone app.")
    parser.add_argument("--quick", action="store_true", help="Run a shorter smoke pass instead of the exhaustive pass.")
    parser.add_argument("--keep-app", action="store_true", help="Leave the launched osci-render process running.")
    parser.add_argument("--native-dialogs", action="store_true", help="Include actions that may open native file dialogs.")
    parser.add_argument("--artifact-dir", help="Write logs, JSON snapshots, screenshots, and traces to this directory.")
    parser.add_argument("--jucewright", help="Use a specific jucewright executable.")
    parser.add_argument("--app", "--app-path", "--app-bundle", dest="app_path", help="Use a specific app bundle or standalone executable.")
    parser.add_argument("--app-executable", help="Executable path used only for preflight existence checks.")
    parser.add_argument("--session", help="Jucewright session name.")
    return parser.parse_args()


def main() -> int:
    run = BrowserRun(parse_args())
    return run.run()


if __name__ == "__main__":
    sys.exit(main())
