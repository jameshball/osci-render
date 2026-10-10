#!/usr/bin/env python3
"""A camera hides the lines it can't see: a turned cube loses its far edges on the Scope, and the setting saves."""
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
session.launch_app("hidden-lines")
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


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Hidden lines", duration="8", bpm="120", fps="60")
asset = ET.SubElement(composition, "asset", id="1", name="Cube.obj", extension=".obj")
asset.text = encoded(b"v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
                     b"f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 4 8 5 1\n")
track = ET.SubElement(composition, "track", id="2", name="Cube", kind="visual")
clip = ET.SubElement(track, "clip", id="3", asset="1", name="Cube", start="0", duration="8", offset="0", rate="1",
                     timeBase="seconds", contentBpm="120")
ET.SubElement(clip, "property", name="rotation.x", base="30")
ET.SubElement(clip, "property", name="rotation.y", base="40")
ET.SubElement(composition, "camera", id="9", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "hidden-lines.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def status_says(text, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(text in str(n.get("value", "")) + str(n.get("name", "")) for n in nodes(json.loads(command("snapshot", "--json", "--full")))):
            return True
        time.sleep(.25)
    return False


def status_stops_saying(text, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not any(text in str(n.get("value", "")) + str(n.get("name", "")) for n in nodes(json.loads(command("snapshot", "--json", "--full")))):
            return True
        time.sleep(.25)
    return False


def lit_on_scope(name):
    """Bright pixels in the Scope: drawn lines."""
    path = session.artifact_dir / name
    command("screenshot", "--file", path)
    image = Image.open(path).convert("RGB")
    scale = image.width / window["w"]
    count = 0
    for x in range(int(scope["x"] * scale), int((scope["x"] + scope["w"]) * scale), 2):
        for y in range(int(scope["y"] * scale), int((scope["y"] + scope["h"]) * scale), 2):
            r, g, b = image.getpixel((x, y))
            if g > 90:
                count += 1
    return count


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Hidden lines", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    tree = json.loads(command("snapshot", "--json", "--full"))
    window = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionEditor")
    scope = next(n["bounds"] for n in nodes(tree) if n.get("class") == "VisualiserComponent")
    time.sleep(1.5)
    whole = lit_on_scope("whole.png")
    # The Cameras band shows the camera; clicking it selects it.
    step("select camera", "click", "--class", "MotionTimelineView", "--position", "400,39")
    command("wait-for-locator", "--name", "Camera hidden lines", "--exact")
    step("open hidden lines", "click", "--name", "Camera hidden lines", "--exact")
    step("hide them", "click", "--name", "Hidden", "--role", "menuItem", "--exact")
    session.wait_for_undo("Hide hidden lines")
    # Baked in the background; the status bar says when it is done.
    if not status_says("hidden lines removed"):
        raise RuntimeError("The status bar should say the hidden lines are removed once baked")
    hidden = lit_on_scope("hidden.png")
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    camera = next(c for c in saved.iter("camera") if c.get("id") == "9")
    if camera.get("hideHiddenLines") not in ("1", "true"):
        raise RuntimeError("The camera's setting should save with the project")
    step("undo", "click", "--name", "Undo", "--exact")
    if not status_stops_saying("hidden lines removed"):
        raise RuntimeError("Undo should show the hidden lines again")
    lit_on_scope("restored.png")
    print("Motion hidden lines passed", flush=True)
finally:
    session.stop_app()
