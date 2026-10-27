#!/usr/bin/env python3
"""Hold a dragged clip, then a trimmed edge, at the timeline's right edge: the timeline scrolls and the clip goes with it."""
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
session.launch_app("motion-edge-scroll")
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


def named_value(name):
    for node in nodes(snapshot()):
        if name in (node.get("name"), node.get("componentName"), node.get("title")):
            return node.get("value")
    raise RuntimeError("Missing " + name)


def seconds(text):
    return float(str(text).rstrip("s"))


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("import the cat", "drop-files", "--file", session.root_dir / "Resources/svg/cat.svg", "--class", "MotionEditor", "--exact")
    session.wait_for_undo("Import source")
    timeline = next(n["bounds"] for n in nodes(snapshot()) if n.get("class") == "MotionTimelineView")
    row = header("cat.svg")[1]
    right = timeline["x"] + timeline["w"] - 4
    step("press on the clip", "mouse-down", 330, row)
    step("carry it to the edge", "mouse-move", 700, row)
    step("hold it at the edge", "mouse-move", right, row)
    step("let it scroll", "wait", "--ms", 1500)
    step("held screenshot", "screenshot", "--file", session.artifact_dir / "held.png", "--scale", "2")
    step("let go", "mouse-up", right, row)
    session.wait_for_undo("Move clip")
    start = seconds(named_value("Clip start"))
    # Without scrolling the clip could move at most about 15 s.
    if start < 20:
        raise RuntimeError("Holding a clip at the edge should scroll the timeline and carry it on: start " + str(start))
    step("moved screenshot", "screenshot", "--file", session.artifact_dir / "moved.png", "--scale", "2")
    # Its right edge, trimmed and held at the edge, lengthens it as it scrolls.
    step("undo the move", "click", "--name", "Undo", "--exact")
    step("back to the start", "click", "--name", "Go to start", "--exact")
    names = next(n["bounds"] for n in nodes(snapshot()) if n.get("name") == "Track header")
    end = names["x"] + names["w"] + round(5 * 70) - 2
    print("names", names, "end", end, "row", row, flush=True)
    step("before trim screenshot", "screenshot", "--file", session.artifact_dir / "before-trim.png", "--scale", "2")
    step("press on the clip's end", "mouse-down", end, row)
    step("trim to the edge", "mouse-move", 900, row)
    step("hold the trim at the edge", "mouse-move", right, row)
    step("let the trim scroll", "wait", "--ms", 1500)
    step("let the trim go", "mouse-up", right, row)
    session.wait_for_undo("Trim clip")
    duration = seconds(named_value("Clip duration"))
    if duration < 20:
        raise RuntimeError("Holding a trimmed edge at the edge should keep lengthening the clip: " + str(duration))
    step("trimmed screenshot", "screenshot", "--file", session.artifact_dir / "trimmed.png", "--scale", "2")
    print("Clip after the trim", duration, flush=True)
    print("Clip start after the move", start, flush=True)
    print("Motion edge scroll passed", flush=True)
finally:
    session.stop_app()
