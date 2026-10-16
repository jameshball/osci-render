#!/usr/bin/env python3
"""A hovered source shows a + that adds it at the playhead; double-clicking empty track space adds the chosen source there."""
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
session.launch_app("motion-add-source")
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


import time


def header(label):
    for node in nodes(snapshot()):
        if str(node.get("componentName", "")).startswith("Track name ") and node.get("value") == label:
            bounds = node["bounds"]
            return [bounds["x"] + bounds["w"] // 2, bounds["y"] + bounds["h"] // 2]
    raise RuntimeError("Missing timeline header " + label)


def find(predicate):
    for node in nodes(snapshot()):
        if predicate(node):
            return node
    return None


def clip_count():
    return sum(1 for node in nodes(snapshot()) if str(node.get("componentName", "")).startswith("Track name "))


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    for source in ["Resources/models/cube.obj", "Resources/svg/cat.svg"]:
        step("import " + source, "drop-files", "--file", session.root_dir / source, "--class", "MotionEditor", "--exact")
        session.wait_for_undo("Import source")
    sources = find(lambda n: n.get("componentID") == "motion.assets" or n.get("name") == "Motion assets")["bounds"]
    cat_row = [sources["x"] + sources["w"] // 2, sources["y"] + 46 + 23]
    step("hover the cat", "mouse-move", *cat_row)
    time.sleep(.3)
    step("hover screenshot", "screenshot", "--file", session.artifact_dir / "hover.png", "--scale", "2")
    plus = [sources["x"] + sources["w"] - 36 + 12, cat_row[1]]
    step("playhead to the start", "click", "--name", "Go to start", "--exact")
    step("click the +", "click-xy", *plus)
    session.wait_for_undo("Add object clip")
    # Double-click empty space on the cube's track, at 8 s: the chosen cat goes there.
    timeline = find(lambda n: n.get("class") == "MotionTimelineView")["bounds"]
    names = find(lambda n: n.get("name") == "Track header")["bounds"]
    x = names["x"] + names["w"] + 8 * 70
    y = header("cube.obj")[1]
    step("double-click empty track space", "click", "--class", "MotionTimelineView", "--position", f"{x - timeline['x']},{y - timeline['y']}", "--click-count", "2")
    session.wait_for_undo("Add object clip")
    time.sleep(.2)
    step("added screenshot", "screenshot", "--file", session.artifact_dir / "added.png", "--scale", "2")
    print("Motion add source passed", flush=True)
finally:
    session.stop_app()
