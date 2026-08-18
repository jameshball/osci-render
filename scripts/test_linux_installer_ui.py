#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Exercise the native Linux installer UI with Jucewright.")
    parser.add_argument("--app", required=True, help="Path to the Debug osci-installer executable.")
    parser.add_argument("--jucewright", help="Path to the jucewright CLI.")
    parser.add_argument("--artifact-dir", help="Directory for screenshots, snapshots and logs.")
    parser.add_argument("--keep-app", action="store_true", help="Leave the final installer process running.")
    return parser.parse_args()


class InstallerBrowser:
    def __init__(self, args: argparse.Namespace):
        self.root = Path(__file__).resolve().parents[1]
        self.app = Path(args.app).resolve()
        default_artifacts = self.root / "build" / "installer-ui" / dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.artifacts = Path(args.artifact_dir or default_artifacts).resolve()
        self.artifacts.mkdir(parents=True, exist_ok=True)
        self.home = self.artifacts / "home"
        self.home.mkdir(parents=True, exist_ok=True)
        self.keep_app = args.keep_app
        self.session = "osci-installer"
        self.jucewright = Path(args.jucewright).resolve() if args.jucewright else self.find_jucewright()

    def find_jucewright(self) -> Path:
        candidates = [
            self.root / "modules/jucewright/build/jucewright_cli_artefacts/jucewright",
            Path.home() / ".cache/osci-render/jucewright-build/jucewright_cli_artefacts/jucewright",
        ]
        for candidate in candidates:
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return candidate
        raise RuntimeError("jucewright CLI not found; pass --jucewright after building it")

    def command(self, *args: str, check: bool = True, env: dict[str, str] | None = None) -> str:
        completed = subprocess.run(
            [str(self.jucewright), *args],
            cwd=self.root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if check and completed.returncode != 0:
            raise RuntimeError(f"jucewright failed ({completed.returncode}): {' '.join(args)}\n{completed.stdout}")
        return completed.stdout

    def session_command(self, *args: str, check: bool = True) -> str:
        return self.command("-s", self.session, *args, check=check)

    def stop(self) -> None:
        output = self.command("list", check=False)
        for line in output.splitlines():
            if line.startswith(self.session + " ") and "pid=" in line:
                pid = line.split("pid=", 1)[1].split()[0]
                subprocess.run(["kill", pid], check=False)
        time.sleep(0.3)

    def launch(self, result: str, label: str) -> None:
        self.stop()
        launch_home = self.home / label
        launch_home.mkdir(parents=True, exist_ok=True)
        env = os.environ.copy()
        env["OSCI_INSTALLER_AUTOMATION_RESULT"] = result
        output = self.command(
            "launch",
            "--app", str(self.app),
            "--app-name", "osci-installer",
            "--session", self.session,
            "--artifact-dir", str(self.artifacts),
            "--home", str(launch_home),
            "--no-profile",
            "--stdout", str(self.artifacts / f"{label}.stdout.log"),
            "--stderr", str(self.artifacts / f"{label}.stderr.log"),
            "--timeout-ms", "30000",
            env=env,
        )
        (self.artifacts / f"{label}.launch.json").write_text(output, encoding="utf-8")
        time.sleep(0.5)

    def screenshot(self, name: str) -> None:
        self.session_command("screenshot", "--target", "root", "--source", "component",
                             "--file", str(self.artifacts / f"{name}.png"), "--no-base64")
        self.session_command("screenshot", "--target", "root", "--source", "native",
                             "--file", str(self.artifacts / f"{name}-native.png"), "--no-base64")

    def snapshot(self, name: str) -> None:
        output = self.session_command("snapshot", "--format", "json")
        json.loads(output)
        (self.artifacts / f"{name}.json").write_text(output, encoding="utf-8")

    def click(self, name: str) -> None:
        self.session_command("click", "--role", "button", "--name", name, "--timeout-ms", "5000")

    def fill(self, component_name: str, value: str) -> None:
        self.session_command("click", "--role", "editableText", "--name", component_name, "--timeout-ms", "5000")
        self.session_command("press", "Control+A", "--role", "editableText", "--name", component_name,
                             "--timeout-ms", "5000")
        self.session_command("type", "--role", "editableText", "--name", component_name,
                             value, "--timeout-ms", "5000")
        self.session_command("press", "Tab", "--role", "editableText", "--name", component_name,
                             "--timeout-ms", "5000")
        time.sleep(0.5)

    def press_button(self, name: str) -> None:
        self.session_command("press", "Enter", "--role", "button", "--name", name, "--timeout-ms", "5000")

    def component_state(self, snapshot_name: str, name: str) -> dict:
        snapshot = json.loads((self.artifacts / f"{snapshot_name}.json").read_text(encoding="utf-8"))

        def find(node: dict) -> dict | None:
            if node.get("name") == name:
                return node
            for child in node.get("children", []):
                match = find(child)
                if match is not None:
                    return match
            return None

        component = find(snapshot["tree"])
        if component is None:
            raise RuntimeError(f"Component not found in {snapshot_name}: {name}")
        return component

    def component_exists(self, snapshot_name: str, name: str) -> bool:
        try:
            self.component_state(snapshot_name, name)
            return True
        except RuntimeError:
            return False

    def wait_for_button(self, name: str) -> None:
        self.session_command("wait-for-locator", "--role", "button", "--name", name, "--timeout-ms", "10000")

    def run_success_flow(self) -> None:
        self.launch("success", "success")
        self.snapshot("01_initial")
        self.screenshot("01_initial")
        self.click("osci-render")
        self.snapshot("02_product")
        self.screenshot("02_product")
        self.click("Install free")
        time.sleep(0.5)
        self.snapshot("02_locations")
        self.screenshot("02_locations")

        self.session_command("press", "Escape", "--role", "dialogWindow", "--name", "Install osci-render free",
                             "--timeout-ms", "5000")
        time.sleep(0.5)
        self.snapshot("02_locations_dismissed")
        if self.component_exists("02_locations_dismissed", "Install osci-render free"):
            raise RuntimeError("Escape did not dismiss the installation locations overlay")
        self.click("Install free")

        self.fill("Standalone application directory", "relative/path")
        self.snapshot("03_invalid_path")
        self.screenshot("03_invalid_path")
        if self.component_state("03_invalid_path", "Confirm installation").get("enabled", True):
            raise RuntimeError("Invalid paths did not leave the install action disabled")

        self.fill("Standalone application directory", "/proc/version")
        self.snapshot("03_unwritable_path")
        self.screenshot("03_unwritable_path")
        if self.component_state("03_unwritable_path", "Confirm installation").get("enabled", True):
            raise RuntimeError("Unwritable paths did not leave the install action disabled")

        long_root = self.home / "success" / "custom" / "a-directory-with-a-deliberately-long-name"
        self.fill("Standalone application directory", str(long_root / "applications"))
        self.fill("VST3 plugin directory", str(long_root / "plugins"))
        self.snapshot("04_custom_paths")
        self.screenshot("04_custom_paths")
        self.click("Use default locations")
        self.snapshot("05_default_paths")
        self.screenshot("05_default_paths")

        self.click("Confirm installation")
        self.session_command("wait-for-text", "Installing application and plugins", "--timeout-ms", "5000")
        self.screenshot("06_progress")
        self.session_command("wait-for-text", "Installation succeeded", "--timeout-ms", "10000")
        time.sleep(0.5)
        self.snapshot("07_success")
        self.screenshot("07_success")
        self.click("Install another product")
        self.click("sosci")
        self.snapshot("08_sosci_premium")
        self.screenshot("08_sosci_premium")
        self.fill("Enter your license key", "TEST-LICENSE-KEY")
        self.session_command("press", "Enter", "--role", "editableText", "--name", "Enter your license key",
                             "--timeout-ms", "5000")
        self.click("Confirm installation")
        self.session_command("wait-for-text", "Installation succeeded", "--timeout-ms", "10000")
        self.click("Install another product")
        self.click("osci-laser")
        self.snapshot("09_osci_laser_premium")
        self.screenshot("09_osci_laser_premium")
        self.fill("Enter your license key", "TEST-LASER-LICENSE-KEY")
        self.session_command("press", "Enter", "--role", "editableText", "--name", "Enter your license key",
                             "--timeout-ms", "5000")
        self.click("Confirm installation")
        self.session_command("wait-for-text", "Installation succeeded", "--timeout-ms", "10000")

    def run_warning_flow(self) -> None:
        self.launch("warning", "warning")
        self.click("osci-render")
        self.click("Install free")
        self.click("Confirm installation")
        self.session_command("wait-for-text", "Installation succeeded", "--timeout-ms", "10000")
        time.sleep(0.5)
        self.snapshot("10_warning")
        self.screenshot("10_warning")

    def run_failure_flow(self) -> None:
        self.launch("failure", "failure")
        self.click("osci-render")
        self.click("Install free")
        self.click("Confirm installation")
        self.session_command("wait-for-text", "test installation could not write", "--timeout-ms", "10000")
        time.sleep(0.5)
        self.snapshot("11_failure")
        self.screenshot("11_failure")
        if self.component_state("11_failure", "Install failed").get("role") != "dialogWindow":
            raise RuntimeError("Install failure is not exposed as an accessible dialog")
        if self.component_state("11_failure", "OK").get("role") != "button":
            raise RuntimeError("Install failure action is not exposed as an accessible button")
        self.session_command("press", "Escape", "--role", "dialogWindow", "--name", "Install failed",
                             "--timeout-ms", "5000")
        time.sleep(0.5)
        self.snapshot("11_failure_dismissed")
        if self.component_exists("11_failure_dismissed", "Install failed"):
            raise RuntimeError("Escape did not dismiss the install failure dialog")

    def run(self) -> int:
        if not self.app.is_file() or not os.access(self.app, os.X_OK):
            raise RuntimeError(f"installer executable is not usable: {self.app}")
        if not self.jucewright.is_file() or not os.access(self.jucewright, os.X_OK):
            raise RuntimeError(f"jucewright executable is not usable: {self.jucewright}")

        try:
            self.run_success_flow()
            self.run_warning_flow()
            self.run_failure_flow()
        finally:
            if not self.keep_app:
                self.stop()

        print(f"Installer UI artifacts: {self.artifacts}")
        return 0


def main() -> int:
    try:
        return InstallerBrowser(parse_args()).run()
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
