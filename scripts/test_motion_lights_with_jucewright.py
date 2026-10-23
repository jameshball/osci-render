#!/usr/bin/env python3
"""Lights: a box over a floor lit by a spot light from Add source > Light, its cone and colour changed in Properties, all saved."""
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
session.launch_app("lights")
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


def scope_colour(name):
    """The Scope's lit pixels, and how much red, green and blue they have in all."""
    path = session.artifact_dir / name
    command("screenshot", "--file", path)
    image = Image.open(path).convert("RGB")
    scale = image.width / window["w"]
    count, totals = 0, [0, 0, 0]
    for x in range(int(scope["x"] * scale), int((scope["x"] + scope["w"]) * scale), 2):
        for y in range(int(scope["y"] * scale), int((scope["y"] + scope["h"]) * scale), 2):
            pixel = image.getpixel((x, y))
            if max(pixel) > 60:
                count += 1
                for channel in range(3):
                    totals[channel] += pixel[channel]
    return count, totals


def settle_bake():
    """Long enough for a short project's frames to be lit."""
    time.sleep(3)


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Lights", duration="8", bpm="120", fps="60")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "lights.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Lights", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    tree = json.loads(command("snapshot", "--json", "--full"))
    window = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionEditor")
    scope = next(n["bounds"] for n in nodes(tree) if n.get("class") == "VisualiserComponent")

    add("Generate", "Box")
    # With no light yet, being lit by lights would do nothing: no row for it.
    field("lit", present=False)
    # White, so a light's colour is what it is drawn in.
    for channel in ("red", "green", "blue"):
        step("white " + channel, "set-value", field(channel)["ref"], "1")
        session.wait_for_undo("Change " + channel.capitalize())
    time.sleep(1)
    unlit, unlit_colour = scope_colour("unlit.png")
    if unlit < 50:
        raise RuntimeError("The Scope should draw the box")

    add("Light", "Spot light")
    if field("light.cone").get("value") in (None, ""):
        raise RuntimeError("A spot light's Properties have its cone")
    field("beam", present=False)
    # A light's opacity only dims it as its intensity does: shown only once used.
    field("opacity", present=False)
    settle_bake()
    time.sleep(1)
    command("screenshot", "--file", session.artifact_dir / "spot.png")
    lit, lit_colour = scope_colour("spot-scope.png")
    if lit < 20:
        raise RuntimeError("The spot light should light the box: " + str(lit))

    # A red light: the box turns red.
    step("no green", "set-value", field("green")["ref"], "0")
    session.wait_for_undo("Change Green")
    step("no blue", "set-value", field("blue")["ref"], "0")
    session.wait_for_undo("Change Blue")
    settle_bake()
    time.sleep(1.5)
    red, red_colour = scope_colour("red.png")
    # A glow's core is white; its colour is in its edges.
    if red_colour[0] < 1.5 * max(red_colour[1], red_colour[2]):
        raise RuntimeError("A red light should light the box red: " + str(red_colour))

    # A wider cone.
    step("wide cone", "set-value", field("light.cone")["ref"], "90")
    session.wait_for_undo("Change Cone")

    # Each kind, with just its own properties.
    expected = {
        "Point light": (["light.intensity", "light.range", "light.shadows"], ["light.cone"]),
        "Directional light": (["light.intensity", "light.shadows"], ["light.range", "light.cone"]),
        "Ambient light": (["light.intensity"], ["light.range", "light.shadows"]),
    }
    for name, (present, absent) in expected.items():
        add("Light", name)
        for property_id in present:
            field(property_id)
        for property_id in absent:
            field(property_id, present=False)
        command("screenshot", "--file", session.artifact_dir / (name.lower().replace(" ", "-") + ".png"))

    # A light already moving on its own, from the presets.
    add("Light", "Fire")
    field("light.range")
    # Its preset, one of a row of them in Properties, changed at a click.
    preset = lambda name: find(lambda n: n.get("name") == "Light presets: " + name)
    if not preset("Fire").get("checked") or preset("Steady").get("checked"):
        raise RuntimeError("A fire light shows its preset")
    step("candle", "click", "--name", "Light presets: Candle", "--exact")
    session.wait_for_undo("Apply candle preset")
    if not preset("Candle").get("checked") or preset("Fire").get("checked"):
        raise RuntimeError("One preset at a time")
    step("fire again", "click", "--name", "Light presets: Fire", "--exact")
    session.wait_for_undo("Apply fire preset")

    step("save", "press", "command + s", "--class", "MotionEditor")
    time.sleep(1)
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    cone = next(p for p in saved.iter("property") if p.get("name") == "light.cone")
    if abs(float(cone.get("value") or cone.get("base") or 0) - 90) > 1e-6:
        raise RuntimeError("The cone should save: " + ET.tostring(cone, encoding="unicode"))
    # Lights shine to the end of the composition.
    fire = [m.get("name") for m in saved.iter("modulator") if m.get("name", "").startswith("Fire \u00b7 ")]
    routes = [r for r in saved.iter("route") if r.get("property") == "light.intensity"]
    if len(fire) != 6 or len(routes) < 3:
        raise RuntimeError("A fire light should save with its six modulators and routes: " + str(fire))
    clips = [clip for clip in saved.iter("clip") if clip.get("name", "").endswith("light")]
    if len(clips) != 4 or any(abs(float(clip.get("start")) + float(clip.get("duration")) - 8) > 1e-6 for clip in clips):
        raise RuntimeError("Four lights, each to the end: " + str([(c.get("name"), c.get("start"), c.get("duration")) for c in clips]))
    print("Motion lights passed", flush=True)
finally:
    session.stop_app()
