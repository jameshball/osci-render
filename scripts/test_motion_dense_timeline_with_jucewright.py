#!/usr/bin/env python3
"""Author twelve layers through imports and verify dense timeline navigation.

This is a usability stress pass, not the finished music-video benchmark.
"""
import json
import math
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
session.launch_app("motion-dense-timeline")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def find(value, predicate):
    if isinstance(value, dict):
        if predicate(value):
            return value
        for child in value.values():
            result = find(child, predicate)
            if result is not None:
                return result
    elif isinstance(value, list):
        for child in value:
            result = find(child, predicate)
            if result is not None:
                return result
    return None


def snapshot():
    return json.loads(command("snapshot", "--json", "--full"))


def track(tree, name):
    return find(tree, lambda n: n.get("componentName", "").startswith("Track name ") and n.get("value") == name)


def timeline(tree):
    return find(tree, lambda n: n.get("class") == "MotionTimelineView")["bounds"]


def visible_header(tree, name):
    node = track(tree, name)
    assert node is not None, "Layer not exposed: " + name
    bounds, area = node["bounds"], timeline(tree)
    # Rows end above the 12px horizontal scroll strip.
    assert bounds["y"] >= area["y"] + 26 and bounds["y"] + bounds["h"] <= area["y"] + area["h"] - 12, (name, bounds, area)
    return node


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Dense navigation study", duration="180", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "dense-navigation.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    step("size dense workspace", "resize-window", "--w", "1440", "--h", "900")
    names = []
    for index in range(12):
        name = f"Orbit {index + 1:02}.obj"
        names.append(name)
        source = session.artifact_dir / name
        sides = index + 3
        points = [f"v {math.cos(i * math.tau / sides):.9f} {math.sin(i * math.tau / sides):.9f} 0" for i in range(sides)]
        source.write_text("\n".join(points + ["f " + " ".join(str(i + 1) for i in range(sides))]) + "\n")
        step("import " + name, "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
        command("wait-for-locator", "--name", name, "--class", "juce::Label", "--exact")
        visible_header(snapshot(), name)
    step("last imported layer visible", "screenshot", "--file", session.artifact_dir / "last-layer.png")
    step("fit entire project", "press", "F", "--class", "MotionTimelineView")
    tree = snapshot()
    first = visible_header(tree, names[0])
    area = timeline(tree)
    assert first["bounds"]["y"] < area["y"] + 58, "Fit left headers at their old scrolled positions"
    step("project overview", "screenshot", "--file", session.artifact_dir / "overview.png")
    step("resize compact workspace", "resize-window", "--w", "1100", "--h", "700")
    area = timeline(snapshot())
    for index in range(15):
        step("scroll layers " + str(index), "wheel", str(area["x"] + 250), str(area["y"] + 60), "--dy", "-0.2")
    tree = snapshot()
    visible_header(tree, names[-1])
    # The final page must retain all the fully fitting rows, rather than allowing
    # the last track to scroll to the top over a large blank area.
    visible_rows = max(1, (area["h"] - 48 - 12) // 32)
    visible_header(tree, names[-visible_rows])
    step("compact last page", "screenshot", "--file", session.artifact_dir / "compact-last-page.png")
    step("fit compact project", "press", "F", "--class", "MotionTimelineView")
    visible_header(snapshot(), names[0])
    area = timeline(snapshot())
    full_rows, partial = divmod(area["h"] - 48 - 12, 32)
    assert partial >= 4, "Fixture needs a partially visible bottom row"
    # Fit uses a 20px right margin and the project's 180-second duration.
    pps = (area["w"] - 220 - 20) / 180
    row_y = 48 + full_rows * 32 + partial // 2
    middle = 220 + round(2.5 * pps)
    step("move partial-row clip horizontally", "drag", "--class", "MotionTimelineView", "--position", f"{middle},{row_y}", "--dx", "100", "--dy", "0", "--steps", "12")
    step("save partial-row edit", "press", "command + s", "--class", "MotionEditor")
    moved_data = project.read_bytes()
    moved = ET.fromstring(moved_data[8:8 + struct.unpack("<I", moved_data[4:8])[0]])
    moved_tracks = moved.findall("./composition/track")
    assert all(len(t.findall("clip")) == 1 for t in moved_tracks), "Horizontal drag changed the owning track"
    assert float(moved_tracks[full_rows].find("clip").get("start")) > 10, "Partial-row drag did not move the selected clip"
    step("undo partial-row edit", "click", "--name", "Undo", "--exact")
    step("save twelve authored layers", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    tracks = saved.findall("./composition/track")
    assert len(tracks) == 12 and [t.get("name") for t in tracks] == names
    assert float(saved.find("composition").get("duration")) == 180
    print("Twelve-layer UI import/navigation/save workflow passed.", flush=True)
finally:
    session.stop_app()
