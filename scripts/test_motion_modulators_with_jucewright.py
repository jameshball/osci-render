#!/usr/bin/env python3
"""Shared modulators, routing, property links and spatial motion modes through the real workspace."""
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
session.launch_app("motion-modulators")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def wait_undo(text):
    command("wait-for-locator", "--name", "Undo " + text, "--role", "label", "--exact", "--timeout-ms", 20000)


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Modulator study", duration="8", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
for track_id, clip_id, name in ((2, 3, "Leader"), (4, 5, "Follower")):
    track = ET.SubElement(composition, "track", id=str(track_id), name=f"Row {track_id}", kind="visual")
    clip = ET.SubElement(track, "clip", id=str(clip_id), asset="1", name=name, start="0", duration="8",
                         offset="0", rate="1", timeBase="seconds", contentBpm="120")
    for group, base in (("position", 0), ("rotation", 0), ("scale", 1)):
        for axis in "xyz":
            prop = ET.SubElement(clip, "property", name=f"{group}.{axis}", base=str(base))
            if clip_id == 3 and group == "position" and axis == "x":
                for t, v in ((0, -0.5), (4, 0.5)):
                    ET.SubElement(prop, "key", time=str(t), value=str(v), interpolation="1", **{"in": "0", "out": "0"})
    for prop, base in (("red", 1), ("green", 1), ("blue", 1), ("weight", 1)):
        ET.SubElement(clip, "property", name=prop, base=str(base))
ET.SubElement(composition, "camera", id="9", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "modulators.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def saved():
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


def clip_property(tree, clip_id, name):
    clip = next(c for c in tree.iter("clip") if c.get("id") == str(clip_id))
    return clip, clip.find(f"property[@name='{name}']")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Modulator study", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    # A shared LFO, edited in the library.
    step("modulator tab", "click", "--name", "Modulators", "--class", "MotionTabs::Tab", "--exact")
    step("add modulator", "click", "--name", "Add modulator", "--exact")
    wait_undo("Add modulator")
    step("saw", "click", "--name", "Shape Saw", "--exact")
    step("rate", "set-value", "--name", "Modulator rate", "2")
    wait_undo("Change modulator")
    step("library", "screenshot", "--file", session.artifact_dir / "library.png")
    # Route it into the leader's position y from the graph column.
    step("select leader", "click", "--class", "MotionTimelineView", "--position", "300,64")
    step("graph", "click", "--name", "Graph", "--class", "MotionTabs::Tab", "--exact")
    step("taller graph", "drag", "--name", "Resize timeline", "--class", "osci::PanelDivider", "--exact", "--position", "20,3", "--dx", 0, "--dy", -220)
    step("all channels", "click", "--name", "Animated channels only", "--exact")
    step("choose position y", "click", "--name", "Curve Position Y", "--exact")
    # Dragging the LFO's card onto the Y field in Properties routes it there.
    tree = json.loads(command("snapshot", "--json", "--full"))
    def first(value, predicate):
        if isinstance(value, dict):
            if predicate(value):
                return value
            value = list(value.values())
        if isinstance(value, list):
            for child in value:
                found = first(child, predicate)
                if found is not None:
                    return found
        return None
    card = first(tree, lambda node: str(node.get("class", "")).endswith("Card") and node.get("componentName") == "LFO 1")["bounds"]
    field = first(tree, lambda node: node.get("componentName") == "position.y")["bounds"]
    step("route lfo by dragging", "drag-xy", card["x"] + 30, card["y"] + card["h"] // 2, field["x"] + field["w"] // 2, field["y"] + field["h"] // 2, "--steps", 20)
    wait_undo("Route modulator")
    step("route amount", "set-value", "--name", "Routed amount LFO 1", "0.4")
    wait_undo("Change route")
    # Link the follower's position x to the leader's, delayed by half a second.
    step("timeline for follower", "click", "--name", "Timeline", "--class", "MotionTabs::Tab", "--exact")
    step("select follower", "click", "--class", "MotionTimelineView", "--position", "300,96")
    step("graph for follower", "click", "--name", "Graph", "--class", "MotionTabs::Tab", "--exact")
    step("choose position x", "click", "--name", "Curve Position X", "--exact")
    step("link menu", "click", "--name", "Link property", "--exact")
    step("leader submenu", "click", "--name", "Leader", "--role", "menuItem", "--exact")
    step("link leader x", "click", "--name", "Position X", "--role", "menuItem", "--exact")
    wait_undo("Link property")
    step("link delay", "set-value", "--name", "Link delay", "0.5")
    step("graph routing", "screenshot", "--file", session.artifact_dir / "routing.png")
    tree = saved()
    modulator = tree.find("modulator")
    assert modulator is not None and modulator.get("waveform") == "2" and float(modulator.get("rateHz")) == 2, ET.tostring(modulator)
    route = tree.find("route")
    assert route is not None and route.get("target") == "3" and route.get("property") == "position.y" and float(route.get("amount")) == 0.4, ET.tostring(route)
    _, linked = clip_property(tree, 5, "position.x")
    link = linked.find("link")
    assert link is not None and link.get("source") == "3" and link.get("property") == "position.x" and float(link.get("delay")) == 0.5, ET.tostring(linked)
    # A spatial path for the leader, from the inspector's Position row.
    step("timeline for leader", "click", "--name", "Timeline", "--class", "MotionTabs::Tab", "--exact")
    step("select leader again", "click", "--class", "MotionTimelineView", "--position", "300,64")
    step("spatial path", "click", "--name", "Spatial path", "--exact")
    wait_undo("Use spatial path")
    clip, _ = clip_property(saved(), 3, "position.x")
    assert clip.get("spatialPath") == "1", clip.attrib
    _, y = clip_property(saved(), 3, "position.y")
    assert len(y.findall("key")) == 2, "enabling the path keys every axis at the shared times"
    # The Position row's modulation chip opens the axis that is driven: the LFO routes into Y.
    step("modulate position", "click", "--name", "Modulate position", "--exact")
    command("wait-for-locator", "--name", "Curve Position Y", "--role", "listItem", "--selected", "--exact", "--timeout-ms", 5000)
    command("wait-for-locator", "--name", "Routed amount LFO 1", "--exact", "--timeout-ms", 5000)
    step("modulate screenshot", "screenshot", "--file", session.artifact_dir / "modulate.png")
    # Deleting the modulator removes its route; undo restores both.
    step("modulator tab again", "click", "--name", "Modulators", "--class", "MotionTabs::Tab", "--exact")
    step("delete modulator", "click", "--name", "Delete modulator", "--exact")
    wait_undo("Delete modulator")
    tree = saved()
    assert tree.find("modulator") is None and tree.find("route") is None
    step("undo delete", "click", "--name", "Undo", "--exact")
    tree = saved()
    assert tree.find("modulator") is not None and tree.find("route") is not None
    print("Shared modulators, routing, links and spatial path passed.", flush=True)
finally:
    session.stop_app()
