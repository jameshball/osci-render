#!/usr/bin/env python3
"""Several objects selected move together: dragging one moves each as far in the world."""
import json
import struct
import subprocess
import time
import xml.etree.ElementTree as ET
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("multi-move")
session.keep_app = keep


def command(*args):
    return subprocess.check_output(session.cli(*[str(arg) for arg in args]), text=True)


def step(label, *args):
    if not session.run_step(label, session.cli(*[str(arg) for arg in args])):
        raise RuntimeError(label)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def find(predicate):
    return next((n for n in nodes(json.loads(command("snapshot", "--json", "--full"))) if predicate(n)), None)


def generate(name):
    step("open sources", "click", "--name", "Add source", "--exact")
    step("open generators", "click", "--name", "Generate")
    step("add " + name, "click", "--name", name, "--role", "menuItem", "--exact")
    session.wait_for_undo("Add " + name.lower())


def values(name):
    """Each clip's base value of a property, by clip name order."""
    step("save", "press", "command + s", "--class", "MotionEditor")
    time.sleep(1)
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    result = []
    for clip in saved.iter("clip"):
        found = next((p for p in clip.iter("property") if p.get("name") == name), None)
        result.append(float(found.get("base", "0")) if found is not None else 0.0)
    return result


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Multi move", duration="8", bpm="120", fps="60")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "multi-move.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Multi move", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    generate("Star")
    generate("Ellipse")
    # The ellipse half a unit to the right.
    step("ellipse right", "set-value", find(lambda n: n.get("componentName") == "position.x" and "ScrubField" in n.get("class", ""))["ref"], "0.5")
    time.sleep(1)
    tree = json.loads(command("snapshot", "--json", "--full"))
    timeline = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionTimelineView")
    scene = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionCompositionView")
    rows = sorted(n["bounds"]["y"] + n["bounds"]["h"] // 2 - timeline["y"] for n in nodes(tree) if n.get("class") == "MotionTrackHeader")
    step("fit", "press", "F", "--class", "MotionTimelineView")
    x = 220 + round(2 * (timeline["w"] - 240) / 8.0)
    step("select star", "click", "--class", "MotionTimelineView", "--position", f"{x},{rows[0]}")
    step("select both", "press", "command + a", "--class", "MotionTimelineView")
    before = values("position.x")
    if abs(before[1] - before[0] - .5) > 1e-6:
        raise RuntimeError("The ellipse should start half a unit right of the star: " + str(before))
    # Drag the star by its top point (half a unit above the middle).
    tip = (scene["x"] + scene["w"] // 2, scene["y"] + scene["h"] // 2 - round(.225 * (min(scene["w"], scene["h"]) - 48)))
    step("move both", "drag-xy", tip[0], tip[1], tip[0] + 60, tip[1], "--steps", 12)
    session.wait_for_undo("Move objects")
    after = values("position.x")
    moved = [b - a for a, b in zip(before, after)]
    if not (moved[0] > .05 and abs(moved[0] - moved[1]) < 1e-6):
        raise RuntimeError("Both objects should move right by as much: " + str(before) + " -> " + str(after))
    print("Motion multi-object move passed", flush=True)
finally:
    session.stop_app()
