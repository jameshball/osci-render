#!/usr/bin/env python3
"""Generator handles in the Scene: a star's tip dragged out sets its outer radius, scrolling on it adds a point, and a grid's columns handle dragged sideways adds columns."""
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
session.launch_app("generator-handles")
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


def parameter(name):
    """A generator parameter's saved value."""
    step("save", "press", "command + s", "--class", "MotionEditor")
    time.sleep(1)
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    found = next((p for p in saved.iter("property") if p.get("name") == name), None)
    return float(found.get("base")) if found is not None else None


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Handles", duration="8", bpm="120", fps="60")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "handles.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Handles", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    scene = next(n["bounds"] for n in nodes(json.loads(command("snapshot", "--json", "--full"))) if n.get("class") == "MotionCompositionView")
    centre = (scene["x"] + scene["w"] // 2, scene["y"] + scene["h"] // 2)
    # The default view shows a unit as about half the frame's height.
    unit = .5 * (min(scene["w"], scene["h"]) - 48)

    generate("Star")
    tip = (centre[0], centre[1] - round(.5 * unit))
    step("hover tip", "hover", *tip)
    step("drag tip out", "drag-xy", tip[0], tip[1], tip[0], tip[1] - round(.25 * unit), "--steps", 12)
    session.wait_for_undo("Change Outer radius")
    radius = parameter("generator.radius")
    if radius is None or not .7 < radius < .8:
        raise RuntimeError("The star's tip dragged a quarter unit out should make its outer radius about 0.75: " + str(radius))
    tip = (tip[0], tip[1] - round(.25 * unit))
    step("hover moved tip", "hover", *tip)
    step("scroll for a point", "wheel", tip[0], tip[1], "--dy", "0.2")
    session.wait_for_undo("Change Points")
    if parameter("generator.points") != 6:
        raise RuntimeError("Scrolling on the tip should add a point")
    step("remove star", "press", "command + z", "--class", "MotionEditor")
    step("remove star 2", "press", "command + z", "--class", "MotionEditor")
    step("remove star 3", "press", "command + z", "--class", "MotionEditor")

    generate("Grid")
    columns = (centre[0] + round(.8 * unit), centre[1])
    step("hover columns", "hover", *columns)
    step("drag columns", "drag-xy", columns[0], columns[1], columns[0] + 26, columns[1] + 30, "--steps", 12)
    session.wait_for_undo("Change Columns")
    if parameter("generator.columns") != 10:
        raise RuntimeError("Two dozen pixels sideways should add two columns: " + str(parameter("generator.columns")))
    print("Motion generator handles passed", flush=True)
finally:
    session.stop_app()
