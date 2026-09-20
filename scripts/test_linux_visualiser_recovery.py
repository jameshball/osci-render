#!/usr/bin/env python3
"""Exercise Linux detached rendering and session restart; requires Pillow, xdotool and wmctrl."""
from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET

from jucewright_osci_browser.session import BrowserSession


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    parser.add_argument("--jucewright", required=True)
    parser.add_argument("--artifact-dir", required=True)
    parser.add_argument("--source-home", default=str(Path.home()))
    parser.add_argument("--audio-output", required=True, help="An available ALSA output device; audio input is disabled.")
    parser.add_argument("--cycles", type=int, default=5)
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error("--cycles must be positive")
    artifacts = Path(args.artifact_dir).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    home = artifacts / "home"
    session = "visualiser-recovery"
    pid = None
    action_number = 0

    def cli(*arguments: str, in_session: bool = True, env: dict | None = None) -> str:
        nonlocal action_number
        command = [args.jucewright, *(["-s", session] if in_session else []), *arguments]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=40, env=env)
        action_number += 1
        (artifacts / f"{action_number:03d}-{arguments[0]}.log").write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f"{arguments}: {result.stdout}")
        return result.stdout

    def action(*arguments: str) -> str:
        return cli(*arguments, "--timeout-ms", "10000")

    def click(role: str, name: str) -> None:
        action("click", "--role", role, "--name", name)

    def windows() -> list[dict]:
        return json.loads(cli("windows"))["windows"]

    def presentation() -> dict:
        return next(window for window in windows() if window["class"] == "VisualiserWindow")

    def assert_rendered(label: str) -> None:
        from PIL import Image
        screenshot = artifacts / f"{label}.png"
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            time.sleep(1)
            window = presentation()
            cli("screenshot", "--target", window["id"], "--source", "native", "--file", str(screenshot), "--no-base64")
            with Image.open(screenshot).convert("RGB") as image:
                content = image.crop((20, 40, image.width - 20, image.height - 20))
                green_pixels = sum(g > 70 and g > 1.5 * r and g > 1.5 * b for r, g, b in content.getdata())
            if green_pixels >= 100:
                return
        raise RuntimeError(f"Detached view stayed blank in {screenshot}; check the selected audio output")

    def launch() -> None:
        nonlocal pid
        # This same profile is deliberately reused to exercise startup restoration.
        BrowserSession.disable_profile_audio_input(settings, args.audio_output)
        payload = json.loads(cli("launch", "--app", str(Path(args.app).resolve()), "--app-name", "osci-render",
                                 "--session", session, "--artifact-dir", str(artifacts), "--home", str(home),
                                 "--no-profile", "--timeout-ms", "30000", in_session=False))
        pid = payload["matchedSession"]["pid"]
        time.sleep(1)

    def close_normally() -> None:
        nonlocal pid
        # Ask the window manager to close the app through its normal title-bar close path.
        ids = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--pid", str(pid)], text=True).split()
        for window in ids:
            title = subprocess.check_output(["xdotool", "getwindowname", window], text=True).strip()
            if "osci-render" in title and "Software Oscilloscope" not in title:
                subprocess.run(["wmctrl", "-ic", hex(int(window))], check=True)
                break
        else:
            raise RuntimeError("Could not find the standalone window to close")
        deadline = time.monotonic() + 20
        def running() -> bool:
            status = Path(f"/proc/{pid}/status")
            return status.exists() and "State:\tZ" not in status.read_text()
        while running() and time.monotonic() < deadline:
            time.sleep(0.25)
        if running():
            if shutil.which("gdb"):
                with (artifacts / "shutdown-threads.log").open("w") as output:
                    subprocess.run(["gdb", "-batch", "-p", str(pid), "-ex", "thread apply all bt", "-ex", "detach"],
                                   stdout=output, stderr=subprocess.STDOUT, timeout=30)
            raise RuntimeError("Closing the app hung")
        pid = None

    profile = json.loads(cli("prepare-juce-profile", "--home", str(home), "--app-name", "osci-render",
                             "--source-home", args.source_home, "--copy-setting", "osci-licensing.settings",
                             "--keep-audio-state", in_session=False))
    settings = Path(profile["settingsFile"])
    BrowserSession.disable_profile_audio_input(settings, args.audio_output)
    tree = ET.parse(settings)
    audio = tree.getroot().find("./VALUE[@name='audioSetup']")
    if audio is None:
        audio = ET.SubElement(tree.getroot(), "VALUE", name="audioSetup")
    setup = audio.find("DEVICESETUP")
    if setup is None:
        setup = ET.SubElement(audio, "DEVICESETUP")
    setup.set("deviceType", "ALSA")
    setup.set("audioDeviceRate", "48000")
    setup.set("audioDeviceBufferSize", "512")
    tree.write(settings)
    fixture = artifacts / "square.svg"
    fixture.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">'
                       '<path d="M 10 10 L 90 10 L 90 90 L 10 90 Z" fill="none" stroke="white"/></svg>')
    try:
        launch()
        snapshot = cli("snapshot", "--format", "text")
        if '"Later"' in snapshot:
            click("button", "Later")
        action("drop-files", "--role", "unspecified", "--name", "Editor: osci-render", "--file", str(fixture))
        if not any(window["class"] == "VisualiserWindow" for window in windows()):
            click("button", "popOut")
        action("resize-window", "--target", presentation()["id"], "--w", "640", "--h", "480")
        assert_rendered("initial")
        click("button", "settings")
        transparency = next(line for line in cli("snapshot", "--format", "text").splitlines()
                            if '"Transparent Background"' in line and "role=toggleButton" in line)
        if "checked=true" not in transparency:
            click("toggleButton", "Transparent Background")
        action("press", "Escape", "--role", "dialogWindow", "--name", "Visualiser Settings")
        for cycle in range(args.cycles):
            for orientation, resolution in (("portrait", "1080 x 1920"), ("landscape", "1920 x 1080")):
                click("menuItem", "Video")
                click("menuItem", "Recording Settings...")
                action("wait-for-locator", "--role", "comboBox", "--name", "Resolution")
                action("select-option", "--role", "comboBox", "--name", "Resolution", "--text", resolution)
                action("press", "Escape", "--role", "dialogWindow", "--name", "Recording Settings")
                assert_rendered(f"{cycle + 1}-{orientation}")
            click("button", "popOut")
            click("button", "popOut")
            assert_rendered(f"{cycle + 1}-reopened")
            print(f"Completed canvas and detached-window cycle {cycle + 1}", flush=True)
        close_normally()
        assert ET.parse(settings).getroot().find("./VALUE[@name='filterState']") is not None
        launch()
        assert_rendered("restored-session")
        close_normally()
        errors = []
        for log in home.rglob("*.log"):
            errors.extend(line for line in log.read_text(errors="replace").splitlines() if "OpenGL error at" in line)
        if errors:
            raise RuntimeError("OpenGL errors during canvas resizing: " + "\n".join(errors[:10]))
        print(f"Passed live rendering and saved-session restoration: {artifacts}")
    finally:
        if pid is not None and Path(f"/proc/{pid}").exists():
            os.kill(pid, 15)


if __name__ == "__main__":
    main()
