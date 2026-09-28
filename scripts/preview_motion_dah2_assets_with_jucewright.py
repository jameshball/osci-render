#!/usr/bin/env python3
"""Import each "dah" source alone and capture its beam, for asset review."""
import json
import subprocess
import sys
from pathlib import Path
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

assets = Path(sys.argv.pop(1)) if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else Path("build/dah/assets")
session = BrowserSession(parse_args())
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("dah-asset-preview")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("workspace size", "resize-window", "--w", 1440, "--h", 900)
    for name in ["Dot.svg", "Trace.lua", "Mouth.lua", "Lissajous.lua", "Spirograph.lua", "Sphere.obj", "Knot.obj",
                 "DAHH.obj", "Tunnel.obj", "Stars.obj", "Ring.svg", "Equaliser.gif", "dahhhhh.txt", "dahh!.txt"]:
        step("import " + name, "drop-files", "--file", (assets / name).resolve(), "--class", "MotionEditor", "--exact")
        if name.endswith(".lua"):
            command("wait-for-locator", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact")
            step("bake length", "fill", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact", "6.4")
            step("bake", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
        elif name.endswith(".gif"):
            command("wait-for-locator", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
            step("prepare", "click", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
        command("wait-for-locator", "--name", "Undo Import object", "--role", "label", "--exact", "--timeout-ms", 60000)
        step("play", "click", "--name", "Play", "--exact")
        command("wait", "--ms", 900)
        step("beam " + name, "screenshot", "--target", "root", "--file", session.artifact_dir / (name + ".png"))
        step("pause", "click", "--name", "Pause", "--exact")
        step("remove " + name, "click", "--name", "Undo", "--exact")
    print("ARTIFACTS", session.artifact_dir)
finally:
    session.stop_app()
