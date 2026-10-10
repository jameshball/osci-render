#!/usr/bin/env python3
"""Generators: a star and a sphere from Add source > Generate, the star's points edited and keyed in Properties, all saved."""
import json
import struct
import subprocess
import time
import xml.etree.ElementTree as ET
from PIL import Image
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("generators")
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


def field(property_id):
    """A Properties field, by the property it edits."""
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        found = find(lambda n: n.get("componentName") == property_id and "ScrubField" in n.get("class", ""))
        if found is not None:
            return found
        time.sleep(.2)
    raise RuntimeError(property_id + " is not in Properties")


def generate(name):
    step("open sources", "click", "--name", "Add source", "--exact")
    step("open generators", "click", "--name", "Generate")
    step("add " + name, "click", "--name", name, "--role", "menuItem", "--exact")
    session.wait_for_undo("Add " + name.lower())


def lit_on_scope(name):
    """Bright pixels in the Scope: drawn lines."""
    path = session.artifact_dir / name
    command("screenshot", "--file", path)
    image = Image.open(path).convert("RGB")
    scale = image.width / window["w"]
    count = 0
    for x in range(int(scope["x"] * scale), int((scope["x"] + scope["w"]) * scale), 2):
        for y in range(int(scope["y"] * scale), int((scope["y"] + scope["h"]) * scale), 2):
            if image.getpixel((x, y))[1] > 90:
                count += 1
    return count


# An empty project with a file of its own, so it saves in place.
root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Generators", duration="8", bpm="120", fps="60")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "generators.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Generators", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    tree = json.loads(command("snapshot", "--json", "--full"))
    window = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionEditor")
    scope = next(n["bounds"] for n in nodes(tree) if n.get("class") == "VisualiserComponent")

    generate("Star")
    if field("generator.points").get("value") not in ("5", "5.0"):
        raise RuntimeError("A new star has five points: " + str(field("generator.points").get("value")))
    time.sleep(1)
    five = lit_on_scope("star-five.png")
    if five < 50:
        raise RuntimeError("The Scope should draw the star")

    # Points keyed: eight at the start, three two seconds in.
    step("eight points", "set-value", field("generator.points")["ref"], "8")
    session.wait_for_undo("Change Points")
    step("key points", "click", "--name", "Key points", "--exact")
    session.wait_for_undo("Set keyframe")
    timeline = find(lambda n: n.get("class") == "MotionTimelineView")["bounds"]
    step("fit", "press", "F", "--class", "MotionTimelineView")
    pps = (timeline["w"] - 220 - 20) / 8.0
    step("playhead to 2 s", "click", "--class", "MotionTimelineView", "--position", f"{220 + round(2 * pps)},12")
    step("three points", "set-value", field("generator.points")["ref"], "3")
    session.wait_for_undo("Change Points")
    time.sleep(1)
    lit_on_scope("star-three.png")

    generate("Sphere")
    if find(lambda n: n.get("componentName") == "generator.rings" and "ScrubField" in n.get("class", "")) is None:
        raise RuntimeError("A sphere's Properties have its rings")

    step("save", "press", "command + s", "--class", "MotionEditor")
    time.sleep(1)
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    kinds = sorted(asset.get("extension") for asset in saved.iter("asset"))
    if kinds != [".generator", ".generator"]:
        raise RuntimeError("Both generators should save as generator sources: " + str(kinds))
    points = next(p for p in saved.iter("property") if p.get("name") == "generator.points")
    values = sorted(float(key.get("value")) for key in points.findall("key"))
    if values != [3.0, 8.0]:
        raise RuntimeError("The star's points should save with both keys: " + str(values))
    print("Motion generators passed", flush=True)
finally:
    session.stop_app()
