#!/usr/bin/env python3
"""Mirrors: a box beside a mirror ball from Add source > Mirror, its reflection on the Scope, Mirror in Properties (and not on a curve), all saved."""
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
session.launch_app("mirrors")
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
        found = find(lambda n: n.get("componentName") == property_id and "ScrubField" in n.get("class", "") and n.get("visible"))
        if (found is not None) == present:
            return found
        time.sleep(.2)
    raise RuntimeError(property_id + (" is not in Properties" if present else " should not be in Properties"))


def tile(name):
    """A Surface style tile: on (checked) or off."""
    found = find(lambda n: n.get("name") == "Surface styles: " + name)
    if found is None:
        raise RuntimeError(name + " is not among the Surface styles")
    return found


def toggle(name, undo):
    step(name, "click", "--name", "Surface styles: " + name, "--exact")
    session.wait_for_undo(undo)


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
ET.SubElement(root, "composition", name="Mirrors", duration="8", bpm="120", fps="60")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "mirrors.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Mirrors", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    tree = json.loads(command("snapshot", "--json", "--full"))
    window = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionEditor")
    scope = next(n["bounds"] for n in nodes(tree) if n.get("class") == "VisualiserComponent")

    add("Generate", "Box")
    set_field("position.x", "-.6", "Change Position X")
    if not str(field("reflects").get("value", "")).startswith("0"):
        raise RuntimeError("A box doesn't reflect until asked: " + str(field("reflects").get("value")))

    add("Mirror", "Mirror ball")
    if not str(field("reflects").get("value", "")).startswith("100"):
        raise RuntimeError("A mirror ball reflects wholly: " + str(field("reflects").get("value")))
    set_field("position.x", ".5", "Change Position X")
    time.sleep(3)
    mirrored = scope_lit("mirror-ball.png")
    if "reflect" not in status():
        raise RuntimeError("The status bar says the frames reflect: " + status())

    # Not reflecting, it draws its own lines, and nothing of the box.
    set_field("reflects", "0", "Change Mirror")
    time.sleep(3)
    plain = scope_lit("plain-ball.png")
    if abs(plain - mirrored) < 20:
        raise RuntimeError("Reflecting should change what the ball draws: " + str((plain, mirrored)))
    set_field("reflects", "100", "Change Mirror")

    # A curve has no surface to reflect with.
    add("Generate", "Arc")
    field("reflects", present=False)
    if find(lambda n: str(n.get("name", "")).startswith("Surface styles: ") and n.get("visible")) is not None:
        raise RuntimeError("A curve has no Surface styles")
    # A flat mirror, ready to place.
    # The quick way: + Mirror, then its picture.
    step("open mirrors", "click", "--name", "+ Mirror", "--exact")
    step("add flat mirror", "click", "--name", "Add a mirror: Flat mirror", "--exact")
    session.wait_for_undo("Add flat mirror")
    if not str(field("reflects").get("value", "")).startswith("100"):
        raise RuntimeError("A flat mirror reflects wholly")
    command("screenshot", "--file", session.artifact_dir / "mirrors.png")

    step("save", "press", "command + s", "--class", "MotionEditor")
    time.sleep(1)
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    reflecting = [p for p in saved.iter("property") if p.get("name") == "reflects" and abs(float(p.get("value") or p.get("base") or 0) - 100) < 1e-6]
    if len(reflecting) != 2:
        raise RuntimeError("Both mirrors should save reflecting: " + str(len(reflecting)))
    print("Motion mirrors passed", flush=True)
finally:
    session.stop_app()
