#!/usr/bin/env python3
"""Line styles: a ball drawn by its outline and a pattern rather than its edges, from the Surface tiles in Properties (not on a curve), shown on the Scope and saved."""
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


def toggle(name, undo, combine=False):
    """A click draws by that style alone; Shift-click adds or removes it.
    Surface closes Properties, so the click by name scrolls the tile into view."""
    if not combine:
        step(name, "click", "--name", "Surface styles: " + name, "--exact")
        session.wait_for_undo(undo)
        return
    # Shift reaches the tile only as a real pointer click (a locator click
    # sets no modifier keys), so it is scrolled into view and clicked there.
    node = tile(name)
    for _ in range(12):
        area = find(lambda n: n.get("class") == "MotionPropertyInspector")["bounds"]
        bounds = node["bounds"]
        if bounds["y"] >= area["y"] + 34 and bounds["y"] + bounds["h"] <= area["y"] + area["h"]:
            break
        command("wheel", area["x"] + area["w"] // 2, area["y"] + area["h"] // 2, "--dy", -0.4)
        time.sleep(.3)
        node = tile(name)
    step(name, "click-xy", bounds["x"] + bounds["w"] // 2, bounds["y"] + bounds["h"] // 2, "--modifiers", "shift")
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
    if not tile("Edges").get("checked") or tile("Outline").get("checked"):
        raise RuntimeError("A surface draws its edges until asked otherwise")
    # Each style's amount is folded under Advanced, every one of them, to mix
    # styles; what only patterns or noise use shows with them.
    step("open advanced settings", "click", "--name", "Advanced settings", "--exact")
    field("edges")
    field("outline")
    field("fillDetail", present=False)
    field("noiseFlow", present=False)
    time.sleep(3)
    edged = scope_lit("edges.png")

    # By its outline and a spiral over it, not its edges.
    toggle("Outline", "Draw as Outline")
    toggle("Spiral", "Turn on Spiral", combine=True)
    if tile("Edges").get("checked") or not tile("Outline").get("checked") or not tile("Spiral").get("checked"):
        raise RuntimeError("A click draws by one style; Shift-click adds another")
    field("fillDetail")
    field("noiseFlow", present=False)
    time.sleep(3)
    styled = scope_lit("outline-spiral.png")
    if abs(styled - edged) < 20:
        raise RuntimeError("Its style should change what the ball draws: " + str((edged, styled)))
    # Nothing on: nothing drawn.
    toggle("Outline", "Turn off Outline", combine=True)
    toggle("Spiral", "Turn off Spiral", combine=True)
    time.sleep(3)
    blank = scope_lit("nothing.png")
    if blank > edged // 4:
        raise RuntimeError("With no lines asked for, the ball draws none: " + str((blank, edged)))
    toggle("Outline", "Draw as Outline")
    toggle("Rings", "Turn on Rings", combine=True)
    set_field("rings", "60", "Change Rings")
    if tile("Rings").get("checked") is not True:
        raise RuntimeError("Part-way on is on")
    # Noise brings how it morphs.
    toggle("Noise", "Turn on Noise", combine=True)
    field("noisePhase")
    field("noiseFlow")
    toggle("Noise", "Turn off Noise", combine=True)
    command("screenshot", "--file", session.artifact_dir / "lines.png")

    # A curve has no surface to draw lines on.
    add("Generate", "Arc")
    if find(lambda n: str(n.get("name", "")).startswith("Surface styles: ") and n.get("visible")) is not None:
        raise RuntimeError("A curve has no Surface styles")

    step("save", "press", "command + s", "--class", "MotionEditor")
    time.sleep(1)
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    def saved_value(name):
        found = [p for p in saved.iter("property") if p.get("name") == name]
        return float(found[0].get("value") or found[0].get("base") or 0) if found else None
    if (saved_value("edges"), saved_value("outline"), saved_value("rings")) != (0, 100, 60):
        raise RuntimeError("The ball's style should save: " + str((saved_value("edges"), saved_value("outline"), saved_value("rings"))))
    print("Motion line styles passed", flush=True)
finally:
    session.stop_app()
