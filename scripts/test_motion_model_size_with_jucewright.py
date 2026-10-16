#!/usr/bin/env python3
"""A 3D object says how much it was scaled to fit, and keeps its file's own size with the Original size switch."""
import json
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
session.launch_app("motion-model-size")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def snapshot():
    return json.loads(command("snapshot", "--json", "--full"))


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("import the teapot", "drop-files", "--file", session.root_dir / "Resources/models/teapot.obj", "--class", "MotionEditor", "--exact")
    session.wait_for_undo("Import source")
    step("fitted screenshot", "screenshot", "--file", session.artifact_dir / "fitted.png", "--scale", "2")
    command("wait-for-locator", "--name", "Original size", "--exact")
    step("keep original size", "click", "--name", "Original size", "--role", "button", "--exact")
    session.wait_for_undo("Keep original size")
    step("original screenshot", "screenshot", "--file", session.artifact_dir / "original.png", "--scale", "2")
    step("undo", "click", "--name", "Undo", "--exact")
    session.wait_for_undo("Import source")
    print("Motion model size passed", flush=True)
finally:
    session.stop_app()
