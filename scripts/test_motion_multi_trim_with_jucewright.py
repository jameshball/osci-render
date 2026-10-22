#!/usr/bin/env python3
"""Several clips selected trim, slip and stretch together: dragging one clip's end moves every selected clip's end by as much."""
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
session.launch_app("multi-trim")
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


def generate(name):
    step("open sources", "click", "--name", "Add source", "--exact")
    step("open generators", "click", "--name", "Generate")
    step("add " + name, "click", "--name", name, "--role", "menuItem", "--exact")
    session.wait_for_undo("Add " + name.lower())


def durations():
    step("save", "press", "command + s", "--class", "MotionEditor")
    time.sleep(1)
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    return sorted(float(clip.get("duration")) for clip in saved.iter("clip"))


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Multi trim", duration="8", bpm="120", fps="60")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "multi-trim.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Multi trim", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    generate("Star")
    generate("Grid")
    tree = json.loads(command("snapshot", "--json", "--full"))
    timeline = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionTimelineView")
    rows = sorted(n["bounds"]["y"] + n["bounds"]["h"] // 2 - timeline["y"] for n in nodes(tree) if n.get("class") == "MotionTrackHeader")
    if len(rows) != 2:
        raise RuntimeError("Two generators should make two tracks: " + str(rows))
    step("fit", "press", "F", "--class", "MotionTimelineView")
    pps = (timeline["w"] - 184 - 20) / 8.0
    x_of = lambda t: 184 + round(t * pps)
    before = durations()
    if before != [5.0, 5.0]:
        raise RuntimeError("Each generator clip starts five seconds long: " + str(before))
    # Both clips selected, then the first one's end dragged a second later.
    step("select first", "click", "--class", "MotionTimelineView", "--position", f"{x_of(2)},{rows[0]}")
    step("add second", "click", "--class", "MotionTimelineView", "--position", f"{x_of(2)},{rows[1]}", "--modifiers", "shift")
    end = x_of(5) - 3
    step("trim both", "drag-xy", timeline["x"] + end, timeline["y"] + rows[0], timeline["x"] + end + round(pps), timeline["y"] + rows[0], "--steps", 12)
    session.wait_for_undo("Trim clips")
    after = durations()
    if after != [6.0, 6.0]:
        raise RuntimeError("Both selected clips should end a second later: " + str(after))
    print("Motion multi-clip trim passed", flush=True)
finally:
    session.stop_app()
