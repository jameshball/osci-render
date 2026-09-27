#!/usr/bin/env python3
"""Verify parent editing-view restoration and unused definition cleanup."""
import json
from PIL import Image, ImageChops
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
session.launch_app("motion-nested-render")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def write_project(root, path):
    xml = ET.tostring(root, encoding="utf-8")
    path.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def read_project(path):
    data = path.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    root = ET.Element("motion-project", schema="1")
    ET.SubElement(root, "composition", name="Nested rendering study", duration="20", bpm="120", fps="30")
    seed = session.artifact_dir / "seed.osci-motion"
    write_project(root, seed)
    subprocess.run(["open", "-a", str(session.app_path), str(seed)], check=True)
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    source = session.artifact_dir / "Motif.txt"
    source.write_text("RETURN")
    step("import motif", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Motif.txt", "--class", "juce::Label", "--exact")
    step("save source", "press", "command + s", "--class", "MotionEditor")
    step("open clip menu", "click", "--class", "MotionTimelineView", "--position", "250,46", "--button", "right")
    step("create composition", "click", "--name", "Create composition from selection", "--role", "menuItem", "--exact")
    step("save created composition", "press", "command + s", "--class", "MotionEditor")
    created = read_project(seed)
    assert len(created.findall("./composition/definition")) == 1
    assert len(created.findall("./composition/track/clip[@composition]")) == 1
    assert len(created.findall("./composition/track")) == 1
    def nodes(value):
        if isinstance(value, dict):
            yield value
            for child in value.values():
                yield from nodes(child)
        elif isinstance(value, list):
            for child in value:
                yield from nodes(child)
    def bounds(class_name):
        tree = json.loads(command("snapshot", "--json", "--full"))
        return next(node["bounds"] for node in nodes(tree) if node.get("class") == class_name and node.get("visible"))
    def crop(path, area):
        return Image.open(path).convert("RGB").crop((area["x"], area["y"], area["x"] + area["w"], area["y"] + area["h"]))
    preview = bounds("MotionCompositionView")
    step("choose parent scale tool", "press", "S", "--class", "MotionCompositionView")
    step("zoom parent preview", "wheel", preview["x"] + preview["w"] // 2, preview["y"] + preview["h"] // 2, "--dy", "-0.3")
    step("open parent graph", "click", "--name", "Graph", "--class", "osci::TabBar::Tab", "--exact")
    graph = bounds("MotionCurveEditor")
    step("zoom graph values", "wheel", graph["x"] + graph["w"] // 2, graph["y"] + graph["h"] // 2, "--dy", "0.25")
    step("pan graph time", "wheel", graph["x"] + graph["w"] // 2, graph["y"] + graph["h"] // 2, "--dx", "0.3")
    command("mouse-move", 20, 20)
    baseline = session.artifact_dir / "parent-before.png"
    step("parent view before navigation", "screenshot", "--file", baseline)
    step("select library composition", "click", "--name", "Composition 1", "--role", "listItem", "--exact", "--position", "6,12")
    step("enter from graph", "click", "--name", "Open composition", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Back to Main", "--exact")
    step("child preview", "screenshot", "--file", session.artifact_dir / "child-preview.png")
    step("return to parent graph", "click", "--name", "Back to Main", "--exact")
    command("mouse-move", 20, 20)
    restored = session.artifact_dir / "parent-restored.png"
    step("restored parent view", "screenshot", "--file", restored)
    assert bounds("MotionCurveEditor") == graph
    assert bounds("MotionCompositionView") == preview
    assert ImageChops.difference(crop(baseline, preview), crop(restored, preview)).getbbox() is None, "Preview camera/tool/selection changed on return"
    assert ImageChops.difference(crop(baseline, graph), crop(restored, graph)).getbbox() is None, "Graph view or target changed on return"
    step("return to timeline", "click", "--name", "Timeline", "--class", "osci::TabBar::Tab", "--exact")
    step("remove placed instance", "press", "Backspace", "--class", "MotionTimelineView")
    step("save unused definition", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    assert not saved.findall("./composition/track/clip[@composition]") and len(saved.findall("./composition/definition")) == 1
    step("unused definition menu", "click", "--name", "Composition 1", "--role", "listItem", "--exact", "--position", "6,12", "--button", "right")
    step("request cleanup", "click", "--name", "Remove unused composition", "--role", "menuItem", "--exact")
    step("cancel cleanup", "click", "--name", "Cancel", "--class", "juce::TextButton", "--exact")
    step("save cancelled cleanup", "press", "command + s", "--class", "MotionEditor")
    assert len(read_project(seed).findall("./composition/definition")) == 1
    step("reopen unused menu", "click", "--name", "Composition 1", "--role", "listItem", "--exact", "--position", "6,12", "--button", "right")
    step("request confirmed cleanup", "click", "--name", "Remove unused composition", "--role", "menuItem", "--exact")
    step("confirm cleanup", "click", "--name", "Remove composition", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Undo Remove unused composition", "--exact")
    step("save cleanup", "press", "command + s", "--class", "MotionEditor")
    cleaned = read_project(seed)
    assert not cleaned.findall("./composition/definition") and len(cleaned.findall("./composition/asset")) == 1
    step("undo definition cleanup", "click", "--name", "Undo", "--exact")
    step("undo instance removal", "click", "--name", "Undo", "--exact")
    step("save restored composition", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    assert len(saved.findall("./composition/definition")) == 1 and len(saved.findall("./composition/track/clip[@composition]")) == 1
    print("Exact preview/graph restoration, unused cleanup cancellation/confirmation and undo passed", flush=True)

finally:
    session.stop_app()
