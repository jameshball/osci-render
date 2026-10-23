#!/usr/bin/env python3
"""A value changed past a clip's start offers to animate; the key diamond says Animate on hover; auto-key animates at once."""
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
session.launch_app("motion-animate-change")
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


def find(predicate):
    for node in nodes(snapshot()):
        if predicate(node):
            return node
    return None


def status_text():
    node = find(lambda n: n.get("componentName") == "Status message" or n.get("name") == "Status message")
    return node.get("value") or node.get("name") if node else None


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("import the cube", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    session.wait_for_undo("Import source")
    timeline = find(lambda n: n.get("class") == "MotionTimelineView")["bounds"]
    names = find(lambda n: n.get("name") == "Track header")["bounds"]
    x_of = lambda seconds: names["x"] + names["w"] - timeline["x"] + round(seconds * 70)
    step("select the clip", "click", "--class", "MotionTimelineView", "--position", f"{x_of(1)},{names['y'] - timeline['y'] + 14}")
    step("playhead to 2 s", "click", "--class", "MotionTimelineView", "--position", f"{x_of(2)},12")
    # Hovering a diamond with no keys yet says what it does.
    key = find(lambda n: n.get("name") == "Key position")["bounds"]
    step("hover the position diamond", "mouse-move", key["x"] + key["w"] // 2, key["y"] + key["h"] // 2)
    time.sleep(.3)
    step("hover screenshot", "screenshot", "--file", session.artifact_dir / "hover.png", "--scale", "2")
    position = find(lambda n: n.get("componentName") == "position.x")
    step("move it", "set-value", position["ref"], "0.4")
    session.wait_for_undo("Change Position X")
    time.sleep(.3)
    step("offer screenshot", "screenshot", "--file", session.artifact_dir / "offer.png", "--scale", "2")
    step("animate this change", "click", "--name", "Animate this change", "--exact")
    session.wait_for_undo("Animate change")
    time.sleep(.2)
    step("animated screenshot", "screenshot", "--file", session.artifact_dir / "animated.png", "--scale", "2")
    # Auto-key: the next change animates at once, in the same undo step.
    step("auto-key on", "click", "--name", "Auto-key", "--exact")
    time.sleep(.2)
    step("auto-key screenshot", "screenshot", "--file", session.artifact_dir / "auto-key.png", "--scale", "2")
    scale = find(lambda n: n.get("componentName") == "scale.x")
    step("scale it", "set-value", scale["ref"], "1.5")
    session.wait_for_undo("Change Scale X")
    time.sleep(.8)
    step("auto-keyed screenshot", "screenshot", "--file", session.artifact_dir / "auto-keyed.png", "--scale", "2")
    step("undo the auto-keyed change", "click", "--name", "Undo", "--exact")
    session.wait_for_undo("Animate change")
    print("Motion animate change passed", flush=True)
finally:
    session.stop_app()
