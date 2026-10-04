#!/usr/bin/env python3
"""The Colour row's swatch opens a picker: dragging in it changes red, green
and blue together as one undo step, and the swatch follows."""
import json
import struct
import subprocess
import xml.etree.ElementTree as ET

from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession


session = BrowserSession(parse_args())
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("colour-picker")
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


def box(node):
    bounds = node["bounds"]
    return bounds["x"], bounds["y"], bounds.get("w", bounds.get("width")), bounds.get("h", bounds.get("height"))


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Colour study", duration="8", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Swatch triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Shape", kind="visual")
clip = ET.SubElement(track, "clip", id="3", asset="1", name="Shape", start="0", duration="8", offset="0", rate="1",
                     timeBase="seconds", contentBpm="120")
for name, base in (("red", 0.2), ("green", 1), ("blue", 0.35)):
    ET.SubElement(clip, "property", name=name, base=str(base))
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "colour.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def saved_colour():
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    tree = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    saved = next(c for c in tree.iter("clip") if c.get("id") == "3")
    return [round(float(saved.find(f"property[@name='{name}']").get("base")), 3) for name in ("red", "green", "blue")]


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Colour study", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    step("select clip", "click", "--class", "MotionTimelineView", "--position", "300,64")
    command("wait-for-locator", "--name", "Colour swatch", "--exact")
    step("swatch", "screenshot", "--file", session.artifact_dir / "colour-swatch.png", "--scale", 2)
    step("open picker", "click", "--name", "Colour swatch", "--exact")
    command("wait-for-locator", "--name", "Colour picker", "--exact")
    step("picker", "screenshot", "--file", session.artifact_dir / "colour-picker.png", "--scale", 2)
    x, y, w, h = box(find(lambda n: n.get("name") == "Colour picker"))
    # Hue strip: blue (two thirds along). Square: full saturation and brightness.
    strip_y = y + 10 + 136 + 10 + 6
    step("pick hue", "drag-xy", x + 10 + 100, strip_y, x + 10 + round(200 * 2 / 3), strip_y, "--steps", 8)
    session.wait_for_undo("Change colour")
    step("pick saturation", "drag-xy", x + 10 + 100, y + 10 + 68, x + 10 + 199, y + 11, "--steps", 8)
    step("picked", "screenshot", "--file", session.artifact_dir / "colour-picked.png", "--scale", 2)
    red, green, blue = saved_colour()
    assert blue > 0.95 and red < 0.1 and green < 0.1, (red, green, blue)
    # One undo step per drag: undoing the saturation drag keeps the hue.
    step("undo saturation", "click", "--name", "Undo", "--exact")
    session.wait_for_undo("Change colour")
    red, green, blue = saved_colour()
    assert blue > red and blue > green, (red, green, blue)
    step("undo hue", "click", "--name", "Undo", "--exact")
    assert saved_colour() == [0.2, 1.0, 0.35]
    print("Motion colour picker passed", flush=True)
finally:
    session.stop_app()
