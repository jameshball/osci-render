#!/usr/bin/env python3
"""Pick several tracks by their names and delete them together, from their menu or with the Delete key."""
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
session.launch_app("motion-track-delete")
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


def named(prefix):
    for node in nodes(snapshot()):
        if str(node.get("componentName", "")).startswith(prefix):
            return node
    raise RuntimeError("Missing " + prefix)


def middle(node):
    bounds = node["bounds"]
    return [bounds["x"] + bounds["w"] // 2, bounds["y"] + bounds["h"] // 2]


def field(label):
    for node in nodes(snapshot()):
        if node.get("componentName") == label:
            return node.get("value")
    raise RuntimeError("Missing field " + label)


def track_names():
    return sorted(node.get("value") for node in nodes(snapshot()) if str(node.get("componentName", "")).startswith("Track name "))


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    for source in ["Resources/models/cube.obj", "Resources/models/teapot.obj", "Resources/svg/cat.svg"]:
        step("import " + source, "drop-files", "--file", session.root_dir / source, "--class", "MotionEditor", "--exact")
        session.wait_for_undo("Import source")
    step("pick the cube", "click-xy", *header("cube.obj"))
    step("add the cat", "click-xy", *header("cat.svg"), "--modifiers", "cmd")
    step("menu on the picked tracks", "click-xy", *header("cat.svg"), "--button", "right")
    step("menu screenshot", "screenshot", "--file", session.artifact_dir / "menu.png", "--scale", "2")
    step("delete both", "click", "--name", "Delete 2 tracks", "--role", "menuItem", "--exact")
    session.wait_for_undo("Delete 2 tracks")
    if track_names() != ["teapot.obj"]:
        raise RuntimeError("Deleting two picked tracks should leave the teapot: " + str(track_names()))
    step("deleted screenshot", "screenshot", "--file", session.artifact_dir / "deleted.png", "--scale", "2")
    step("undo", "click", "--name", "Undo", "--exact")
    if len(track_names()) != 3:
        raise RuntimeError("Undo should bring both tracks back")
    # The Delete key does the same for picked track names.
    step("pick the teapot", "click-xy", *header("teapot.obj"))
    step("add the cube", "click-xy", *header("cube.obj"), "--modifiers", "cmd")
    step("press Delete", "press", "Backspace", "--class", "MotionTimelineView")
    session.wait_for_undo("Delete 2 tracks")
    if track_names() != ["cat.svg"]:
        raise RuntimeError("The Delete key should delete the picked tracks: " + str(track_names()))
    # One track picked goes alone.
    step("pick the cat", "click-xy", *header("cat.svg"))
    step("press Delete again", "press", "Backspace", "--class", "MotionTimelineView")
    session.wait_for_undo("Delete track")
    if track_names() != []:
        raise RuntimeError("The Delete key should delete one picked track")
    print("Motion track delete passed", flush=True)
finally:
    session.stop_app()
