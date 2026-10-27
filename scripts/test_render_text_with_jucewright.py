#!/usr/bin/env python3
"""Regression coverage for text parser font lifetime across source changes."""
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
session.launch_app("text-lifetime")
session.keep_app = keep


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


try:
    subprocess.run(session.cli("wait-for-locator", "--class", "OscirenderAudioProcessorEditor", "--exact"), check=True, capture_output=True)
    text = session.artifact_dir / "lifetime-text.txt"
    text.write_text("osci-motion\nHello World", encoding="utf-8")
    for index in range(6):
        step(f"load text {index}", "drop-files", "--file", text)
        step(f"render text {index}", "wait", "--ms", 500)
        step(f"inspect text {index}", "snapshot", "--json", "--interesting", "--depth", 8)
        step(f"switch source {index}", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj")
    step("restore text", "drop-files", "--file", text)
    step("sustain text rendering", "wait", "--ms", 2000)
    step("text screenshot", "screenshot", "--file", session.artifact_dir / "text-rendering.png")
    print("Render text lifetime smoke passed", flush=True)
finally:
    session.stop_app()
