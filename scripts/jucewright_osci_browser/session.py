from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import json
import os
import platform
import re
import shutil
import signal
import subprocess
import time
from pathlib import Path

from .errors import StepError
from .platform_support import (
    default_app_executable,
    default_app_path,
    default_build_dir,
    executable_name,
    is_executable,
    is_linux,
    is_macos,
    is_windows,
    process_exists,
    user_cache_root,
)
from .utils import slug


class BrowserSession:
    def __init__(self, args: argparse.Namespace, root_dir: Path | None = None):
        self.root_dir = root_dir or Path(__file__).resolve().parents[2]
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
