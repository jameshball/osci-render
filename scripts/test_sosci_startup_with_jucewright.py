#!/usr/bin/env python3
"""Regression checks for SOSCI startup/handoff and shared popout preferences.

Uses an isolated profile and the real Debug standalone. System Audio permission
must already be granted; unsupported/denied capture fallback needs separate QA.
"""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import wave
import xml.etree.ElementTree as ET

from jucewright_osci_browser.session import BrowserSession


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    parser.add_argument("--jucewright", required=True)
    args = parser.parse_args()
    home = Path(tempfile.mkdtemp(prefix="sosci-startup-test-"))
    pids = []

    def cli(*command, check=True):
        return subprocess.run([args.jucewright, *command], text=True, capture_output=True,
                              timeout=30, check=check).stdout

    def snapshot(session):
        return cli("-s", session, "snapshot", "--interesting", "--depth", "3", check=False)

    def control(session, name):
        return next((line for line in snapshot(session).splitlines() if f'"{name}"' in line), "")

    def wait_for(description, predicate):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if predicate():
                print(f"PASS: {description}", flush=True)
                return
            time.sleep(0.1)
        raise AssertionError(description)

    def click(session, name):
        cli("-s", session, "click", "--role", "button", "--name", name)

    def new_project(session):
        # Replacing the editor also replaces its automation endpoint.
        cli("-s", session, "select-option", "--role", "menuItem", "--name", "File",
            "--text", "Create New Project", check=False)

    def preference():
        path = settings_file.with_name("sosci_globals.settings")
        if not path.exists():
            return None
        entry = ET.parse(path).getroot().find("VALUE[@name='popoutOpen']")
        return entry.get("val") if entry is not None else None

    def launch(session):
        result = json.loads(cli("launch", "--app", args.app, "--app-name", "sosci",
                                "--session", session, "--home", str(home), "--no-profile",
                                "--artifact-dir", str(home / session), "--timeout-ms", "20000"))
        pids.append(result["matchedSession"]["pid"])

    try:
        profile = json.loads(cli("prepare-juce-profile", "--home", str(home), "--app-name", "sosci",
                                 "--copy-setting", "osci-licensing.settings", "--keep-audio-state"))
        settings_file = Path(profile["settingsFile"])
        BrowserSession.disable_profile_audio_input(settings_file)
        # Fresh popout defaults, without changing the real user's preferences.
        globals_file = settings_file.with_name("sosci_globals.settings")
        globals_file.write_text("<PROPERTIES/>\n")
        fixture = home / "replacement.wav"
        with wave.open(str(fixture), "wb") as output:
            output.setnchannels(2)
            output.setsampwidth(2)
            output.setframerate(44100)
            output.writeframes(bytes(44100 * 4 * 10))

        a, b = f"startup-a-{os.getpid()}", f"startup-b-{os.getpid()}"
        launch(a)
        wait_for("fresh SOSCI opens its popout", lambda: "checked=true" in control(a, "popOut"))
        assert preference() is None, "automatic opening must not save an explicit preference"
        wait_for("intro hands over to input", lambda: "checked=true" in control(a, "audioInput"))
        cli("-s", a, "select-option", "--role", "menuItem", "--name", "Audio", "--text", "Settings")
        wait_for("audio settings ready", lambda: "Audio device type:" in cli(
            "-s", a, "snapshot", "--interesting", "--depth", "12"))
        if "Enable System Audio Capture" in cli("-s", a, "snapshot", "--interesting", "--depth", "12"):
            click(a, "Enable System Audio Capture")
            wait_for("manual capture mutes output", lambda: 'value="On"' in control(a, "VolumeButton"))
        click(a, "Close icon")

        new_project(a)
        wait_for("intro temporarily unmutes output", lambda: 'value="Off"' in control(a, "VolumeButton"))
        wait_for("intro completion restores mute", lambda: 'value="On"' in control(a, "VolumeButton"))
        click(a, "popOut")
        wait_for("explicit close saved", lambda: preference() == "0")
        wait_for("popout closed", lambda: "checked=true" not in control(a, "popOut"))
        click(a, "popOut")
        time.sleep(0.3)
        assert "checked=true" in control(a, "audioInput"), "reopening must not replay the intro"

        for delay in (0.0, 1.8, 2.0):
            new_project(a)
            wait_for("replacement test intro ready", lambda: 'value="Off"' in control(a, "VolumeButton"))
            time.sleep(delay)
            cli("-s", a, "drop-files", "--name", "Editor: sosci", "--file", str(fixture))
            time.sleep(2.5)
            assert "checked=true" not in control(a, "audioInput"), "handoff closed the replacement file"
        print("PASS: replacing the intro preserves the selected file", flush=True)

        new_project(a)
        wait_for("invalid-file test intro ready", lambda: 'value="Off"' in control(a, "VolumeButton"))
        cli("-s", a, "drop-files", "--name", "Editor: sosci", "--file", str(home / "missing.wav"))
        wait_for("invalid file does not cancel handoff", lambda: "checked=true" in control(a, "audioInput"))

        click(a, "popOut")
        launch(b)
        assert "checked=true" not in control(b, "popOut"), "second instance ignored saved close"
        click(a, "popOut")
        new_project(b)
        wait_for("second instance restores latest open", lambda: "checked=true" in control(b, "popOut"))
        click(b, "popOut")
        new_project(a)
        wait_for("first instance respects latest close", lambda: bool(control(a, "popOut"))
                 and "checked=true" not in control(a, "popOut"))
        assert preference() == "0", "automatic restoration overwrote explicit close"
        print(f"PASS: popout preferences across instances; artifacts: {home}", flush=True)
    finally:
        for pid in pids:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass


if __name__ == "__main__":
    main()
