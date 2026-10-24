#!/usr/bin/env python3
"""Tempo changes from the ruler, the camera track band, and camera look-at through the real workspace."""
import json
import struct
import subprocess
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
session.launch_app("motion-tempo-camera")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def wait_undo(text):
    session.wait_for_undo(text, 20.0)


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


# 120 BPM, beats display; one musical clip on beats 8-12 (4-6 s), two cameras.
root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Tempo study", duration="12", bpm="120", fps="30", timeDisplay="2", snapBeats="1")
asset = ET.SubElement(composition, "asset", id="1", name="Triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Beat row", kind="visual")
clip = ET.SubElement(track, "clip", id="3", asset="1", name="On the beat", start="8", duration="4", offset="0", rate="1", timeBase="beats", contentBpm="120")
for group, base in (("position", 0), ("rotation", 0), ("scale", 1)):
    for axis in "xyz":
        ET.SubElement(clip, "property", name=f"{group}.{axis}", base=str(base))
for prop, base in (("red", 1), ("green", 1), ("blue", 1), ("opacity", 100)):
    ET.SubElement(clip, "property", name=prop, base=str(base))
ET.SubElement(composition, "camera", id="9", name="Front")
ET.SubElement(composition, "camera", id="10", name="Side")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "tempo.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def find(value, predicate):
    if isinstance(value, dict):
        if predicate(value):
            return value
        for child in value.values():
            found = find(child, predicate)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find(child, predicate)
            if found is not None:
                return found
    return None


def saved():
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Tempo study", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    step("fit", "press", "F", "--class", "MotionTimelineView")
    # Two cameras show the camera band below the ruler (26-48 px).
    step("band", "screenshot", "--file", session.artifact_dir / "band.png")
    step("cut menu", "click", "--class", "MotionTimelineView", "--position", "614,37", "--button", "right")
    step("cut submenu", "click", "--name", "Cut to camera here", "--role", "menuItem", "--exact")
    step("cut to side", "click", "--name", "Side", "--role", "menuItem", "--exact")
    wait_undo("Cut to camera")
    tree = saved()
    cut = tree.find("cameraCut")
    assert cut is not None and cut.get("camera") == "10", ET.tostring(tree)
    start = float(cut.get("start"))
    # Delete the cut with the keyboard after selecting it.
    step("select cut", "click", "--class", "MotionTimelineView", "--position", "714,37")
    step("delete cut", "press", "Delete", "--class", "MotionTimelineView")
    wait_undo("Delete cut")
    assert saved().find("cameraCut") is None
    step("undo cut delete", "click", "--name", "Undo", "--exact")
    assert saved().find("cameraCut") is not None
    # Dragging the cut moves it as it goes and is one undo step when let go.
    view = find(json.loads(command("snapshot", "--json", "--full")), lambda node: node.get("class") == "MotionTimelineView")["bounds"]
    step("drag cut", "drag-xy", view["x"] + 724, view["y"] + 37, view["x"] + 664, view["y"] + 37, "--steps", 10)
    wait_undo("Move camera cut")
    moved = float(saved().find("cameraCut").get("start"))
    assert moved < start, (moved, start)
    step("undo cut move", "click", "--name", "Undo", "--exact")
    assert abs(float(saved().find("cameraCut").get("start")) - start) < 1e-9
    # A tempo change at beat 9 (the clip's second beat) from the ruler.
    step("tempo menu", "click", "--class", "MotionTimelineView", "--position", "414,12", "--button", "right")
    step("add tempo", "click", "--name", "Add tempo change here...", "--role", "menuItem", "--exact")
    command("wait-for-locator", "--name", "Tempo change BPM", "--timeout-ms", 10000)
    step("bpm", "fill", "--name", "Tempo change BPM", "--class", "juce::TextEditor", "--exact", "60")
    step("apply", "press", "Return", "--name", "Tempo change BPM", "--class", "juce::TextEditor", "--exact")
    wait_undo("Add tempo change")
    tree = saved()
    tempo = tree.find("tempo")
    assert tempo is not None and float(tempo.get("bpm")) == 60 and float(tempo.get("beat")) > 0, ET.tostring(tree)
    musical = tree.find("track/clip")
    assert musical.get("start") == "8" and musical.get("duration") == "4", "musical clips keep their beats"
    step("tempo", "screenshot", "--file", session.artifact_dir / "tempo.png")
    # Aim the front camera at the clip: select it in the Cameras band (under
    # the markers and tempo band), then edit it in Properties.
    step("select camera", "click", "--class", "MotionTimelineView", "--position", "314,59")
    step("look at", "select-option", "--name", "Camera look at", "--text", "On the beat")
    wait_undo("Change camera rig")
    camera = next(c for c in saved().iter("camera") if c.get("id") == "9")
    assert camera.get("target") == "3", camera.attrib
    print("Tempo changes, camera band and look-at passed.", flush=True)
finally:
    session.stop_app()
