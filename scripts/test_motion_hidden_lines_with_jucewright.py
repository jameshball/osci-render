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


def lit_once_baked(name, done, timeout=20.0):
    """The Scope's lit count, once `done` holds for it (the bake runs in the background)."""
    deadline = time.monotonic() + timeout
    lit = lit_on_scope(name)
    while not done(lit) and time.monotonic() < deadline:
        time.sleep(.5)
        lit = lit_on_scope(name)
    return lit


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
    # Hide lines is a camera property in Properties: 100% removes them.
    hide = None
    for _ in range(25):
        hide = next((n for n in nodes(json.loads(command("snapshot", "--json", "--full"))) if n.get("componentName") == "hideLines" and "ScrubField" in n.get("class", "") and n.get("visible")), None)
        if hide is not None:
            break
        time.sleep(.2)
    if hide is None:
        raise RuntimeError("The camera's Properties should have Hide lines")
    step("open hide lines", "click", hide["ref"], "--click-count", "2")
    editor = next((n for n in nodes(next(n for n in nodes(json.loads(command("snapshot", "--json", "--full"))) if n.get("componentName") == "hideLines" and "ScrubField" in n.get("class", ""))) if n.get("class") == "juce::TextEditor"), None)
    if editor is None:
        raise RuntimeError("Hide lines should open for typing")
    step("hide them", "fill", editor["ref"], "100")
    step("commit hide lines", "press", "Return")
    # Baked in the background: the lines behind the faces leave the Scope.
    hidden = lit_once_baked("hidden.png", lambda lit: lit < whole * .95)
    if hidden >= whole * .95:
        raise RuntimeError(f"Hiding lines should leave fewer lines on the Scope ({hidden} lit of {whole})")
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    camera = next(c for c in saved.iter("camera") if c.get("id") == "9")
    hide_lines = next((p for p in camera.iter("property") if p.get("name") == "hideLines"), None)
    if hide_lines is None or float(hide_lines.get("base")) != 100:
        raise RuntimeError("The camera's Hide lines should save with the project")
    step("undo", "click", "--name", "Undo", "--exact")
    restored = lit_once_baked("restored.png", lambda lit: lit > whole * .95)
    if restored <= whole * .95:
        raise RuntimeError(f"Undo should show the hidden lines again ({restored} lit of {whole})")
    print("Motion hidden lines passed", flush=True)
finally:
    session.stop_app()
