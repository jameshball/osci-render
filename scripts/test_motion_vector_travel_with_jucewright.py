#!/usr/bin/env python3
"""Inspect disconnected vector paths in the editing preview and live beam."""
import subprocess
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-vector-travel")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


source = session.artifact_dir / "Separate paths.svg"
source.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100">'
                  '<path fill="none" stroke="white" d="M10 10 H70 V90 H10 Z M130 10 H190 V90 H130 Z"/>'
                  '</svg>')
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("import separated vector paths", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Undo Import object", "--role", "label", "--exact")
    command("wait", "--ms", 500)
    step("frozen beam screenshot", "screenshot", "--file", session.artifact_dir / "frozen.png")
    step("start live timeline", "click", "--name", "Play", "--exact")
    command("wait-for-locator", "--name", "Pause", "--exact")
    command("wait", "--ms", 1000)
    step("playing beam screenshot", "screenshot", "--file", session.artifact_dir / "playing.png")
    step("stop timeline", "click", "--name", "Pause", "--exact")
    step("compact workspace", "resize-window", "--w", 1100, "--h", 700)
    step("compact beam screenshot", "screenshot", "--file", session.artifact_dir / "compact.png")
    print("Motion vector travel workflow passed; inspect screenshots for cross-path chords.", flush=True)
finally:
    session.stop_app()
