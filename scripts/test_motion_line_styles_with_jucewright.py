#!/usr/bin/env python3
"""Line styles: a ball drawn by its outline and a pattern rather than its edges, from Lines in Properties (not on a curve), shown on the Scope and saved."""
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
session.launch_app("line-styles")
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


def field(property_id, present=True):
    """A Properties field, by the property it edits."""
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        found = find(lambda n: n.get("componentName") == property_id and "ScrubField" in n.get("class", ""))
        if (found is not None) == present:
            return found
        time.sleep(.2)
    raise RuntimeError(property_id + (" is not in Properties" if present else " should not be in Properties"))


def add(menu, name):
    step("open sources", "click", "--name", "Add source", "--exact")
    step("open " + menu.lower(), "click", "--name", menu, "--role", "menuItem", "--exact")
    step("add " + name, "click", "--name", name, "--role", "menuItem", "--exact")
    session.wait_for_undo("Add " + name.lower())


def set_field(property_id, value, undo):
    step(property_id + " " + str(value), "set-value", field(property_id)["ref"], str(value))
    session.wait_for_undo(undo)


def scope_lit(name):
    """How many of the Scope's pixels are lit."""
    path = session.artifact_dir / name
    command("screenshot", "--file", path)
    image = Image.open(path).convert("RGB")
    scale = image.width / window["w"]
    count = 0
    for x in range(int(scope["x"] * scale), int((scope["x"] + scope["w"]) * scale), 2):
        for y in range(int(scope["y"] * scale), int((scope["y"] + scope["h"]) * scale), 2):
            count += 1 if max(image.getpixel((x, y))) > 60 else 0
    return count


def status():
    """The status bar's text: what the frames are baked for."""
    texts = (str(n.get("name", "")) + " " + str(n.get("value", "")) for n in nodes(json.loads(command("snapshot", "--json", "--full"))))
    return " ".join(text for text in texts if "kHz" in text)


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Lines", duration="8", bpm="120", fps="60")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "lines.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Lines", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    tree = json.loads(command("snapshot", "--json", "--full"))
    window = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionEditor")
    scope = next(n["bounds"] for n in nodes(tree) if n.get("class") == "VisualiserComponent")

    add("Generate", "Sphere")
    if not str(field("edges").get("value", "")).startswith("100"):
        raise RuntimeError("A surface draws its edges wholly until asked otherwise: " + str(field("edges").get("value")))
    time.sleep(3)
    edged = scope_lit("edges.png")

    # By its outline and a spiral over it, not its edges.
    set_field("edges", "0", "Change Edges")
    set_field("outline", "100", "Change Outline")
    set_field("spiral", "100", "Change Spiral")
    time.sleep(3)
    styled = scope_lit("outline-spiral.png")
    if abs(styled - edged) < 20:
        raise RuntimeError("Its style should change what the ball draws: " + str((edged, styled)))
    # Edges at 0 and nothing else: nothing at all.
    set_field("outline", "0", "Change Outline")
    set_field("spiral", "0", "Change Spiral")
    time.sleep(3)
    blank = scope_lit("nothing.png")
    if blank > edged // 4:
        raise RuntimeError("With no lines asked for, the ball draws none: " + str((blank, edged)))
    set_field("outline", "100", "Change Outline")
    set_field("hilbert", "60", "Change Hilbert curve")
    command("screenshot", "--file", session.artifact_dir / "lines.png")

    # A curve has no surface to draw lines on.
    add("Generate", "Arc")
    field("outline", present=False)

    step("save", "press", "command + s", "--class", "MotionEditor")
    time.sleep(1)
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    def saved_value(name):
        found = [p for p in saved.iter("property") if p.get("name") == name]
        return float(found[0].get("value") or found[0].get("base") or 0) if found else None
    if (saved_value("edges"), saved_value("outline"), saved_value("hilbert")) != (0, 100, 60):
        raise RuntimeError("The ball's style should save: " + str((saved_value("edges"), saved_value("outline"), saved_value("hilbert"))))
    print("Motion line styles passed", flush=True)
finally:
    session.stop_app()
