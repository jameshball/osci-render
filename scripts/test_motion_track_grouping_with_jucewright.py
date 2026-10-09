#!/usr/bin/env python3
"""Pick several tracks by their names (Cmd and Shift) and group them in one go."""
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
session.launch_app("motion-track-grouping")
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


def header(label):
    """The middle of a track's name in its header, in window coordinates."""
    for node in nodes(snapshot()):
        if str(node.get("componentName", "")).startswith("Track name ") and node.get("value") == label:
            bounds = node["bounds"]
            return [bounds["x"] + bounds["w"] // 2, bounds["y"] + bounds["h"] // 2]
    raise RuntimeError("Missing timeline header " + label)


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    for source in ["Resources/models/cube.obj", "Resources/models/teapot.obj", "Resources/svg/cat.svg"]:
        step("import " + source, "drop-files", "--file", session.root_dir / source, "--class", "MotionEditor", "--exact")
        session.wait_for_undo("Import object")
    step("pick the cube", "click-xy", *header("cube.obj"))
    step("add the cat", "click-xy", *header("cat.svg"), "--modifiers", "cmd")
    step("menu on the picked tracks", "click-xy", *header("cat.svg"), "--button", "right")
    step("group both", "click", "--name", "Group 2 tracks", "--role", "menuItem", "--exact")
    session.wait_for_undo("Group tracks")
    tree = snapshot()
    groups = [node for node in nodes(tree) if str(node.get("componentName", "")).startswith("Track name ") and str(node.get("value", "")).startswith("Group ")]
    if len(groups) != 1:
        raise RuntimeError("Grouping two picked tracks should make one group")
    step("grouped screenshot", "screenshot", "--file", session.artifact_dir / "grouped.png", "--scale", "2")
    # Cmd+G groups the selected track too.
    step("pick the teapot", "click-xy", *header("teapot.obj"))
    step("group it", "press", "command + g", "--class", "MotionEditor")
    session.wait_for_undo("Group track")
    print("Motion track grouping passed", flush=True)
finally:
    session.stop_app()
