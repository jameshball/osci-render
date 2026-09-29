#!/usr/bin/env python3
"""Keyframe lanes, key clipboard and source rename through the real workspace."""
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
session.launch_app("motion-keyframe-lanes")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
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
composition = ET.SubElement(root, "composition", name="Lane study", duration="8", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Lane triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
for track_id, clip_id, start, keyed in ((2, 3, 0, True), (4, 5, 4, False)):
    track = ET.SubElement(composition, "track", id=str(track_id), name=f"Row {track_id}", kind="visual")
    clip = ET.SubElement(track, "clip", id=str(clip_id), asset="1", name=f"Clip {clip_id}", start=str(start), duration="4",
                         offset="0", rate="1", timeBase="seconds", contentBpm="120")
    for group, base in (("position", 0), ("rotation", 0), ("scale", 1)):
        for axis in "xyz":
            prop = ET.SubElement(clip, "property", name=f"{group}.{axis}", base=str(base))
            if keyed and group == "position" and axis == "x":
                for t, v in ((0, -0.5), (1, 0), (2, 0.5)):
                    ET.SubElement(prop, "key", time=str(t), value=str(v), interpolation="1", **{"in": "0", "out": "0"})
    for name, base in (("red", 1), ("green", 1), ("blue", 1), ("weight", 1)):
        ET.SubElement(clip, "property", name=name, base=str(base))
ET.SubElement(composition, "camera", id="9", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "lanes.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def saved():
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


def keys(tree, clip_id, prop="position.x"):
    clip = next(c for c in tree.iter("clip") if c.get("id") == str(clip_id))
    return [(round(float(k.get("time")), 4), round(float(k.get("value")), 4)) for k in clip.find(f"property[@name='{prop}']").findall("key")]


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Lane study", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    timeline = find(lambda n: n.get("class") == "MotionTimelineView")["bounds"]
    step("fit", "press", "F", "--class", "MotionTimelineView")
    pps = (timeline["w"] - 170 - 20) / 8.0
    x_of = lambda t: 170 + round(t * pps)
    step("select keyed clip", "click", "--class", "MotionTimelineView", "--position", f"{x_of(0.5)},42")
    step("show lanes", "press", "U", "--class", "MotionTimelineView")
    lane_y = 26 + 32 + 11
    step("lanes", "screenshot", "--file", session.artifact_dir / "lanes.png")
    # Drag the key at 1 s to 1.5 s (Alt: no snapping, then check exact grid-free time).
    step("select middle key", "click", "--class", "MotionTimelineView", "--position", f"{x_of(1)},{lane_y}")
    step("drag middle key", "drag-xy", timeline["x"] + x_of(1), timeline["y"] + lane_y, timeline["x"] + x_of(1.5), timeline["y"] + lane_y, "--steps", 10)
    moved = keys(saved(), 3)
    assert [t for t, _ in moved] == [0, 1.5, 2], moved
    step("undo key move", "click", "--name", "Undo", "--exact")
    assert [t for t, _ in keys(saved(), 3)] == [0, 1, 2]
    # Copy the last two keys and paste them onto the second clip at 5 s.
    step("select key 1", "click", "--class", "MotionTimelineView", "--position", f"{x_of(1)},{lane_y}")
    step("add key 2", "click", "--class", "MotionTimelineView", "--position", f"{x_of(2)},{lane_y}", "--modifiers", "shift")
    step("copy keys", "press", "command + c", "--class", "MotionTimelineView")
    step("select second clip", "click", "--class", "MotionTimelineView", "--position", f"{x_of(5)},{26 + 32 + 22 + 16}")
    step("seek 5 s", "click", "--class", "MotionTimelineView", "--position", f"{x_of(5)},12")
    step("paste keys", "press", "command + v", "--class", "MotionTimelineView")
    pasted = keys(saved(), 5)
    assert len(pasted) == 2 and abs(pasted[1][0] - pasted[0][0] - 1) < 1e-6 and pasted[0][1] == 0 and pasted[1][1] == 0.5, pasted
    # Delete one key from the first clip's lane.
    step("select first key", "click", "--class", "MotionTimelineView", "--position", f"{x_of(0) + 1},{lane_y}")
    step("delete key", "press", "Delete", "--class", "MotionTimelineView")
    assert [t for t, _ in keys(saved(), 3)] == [1, 2]
    # Rename the source inline from the library context menu.
    step("source menu", "click", "--name", "Lane triangle.obj", "--role", "listItem", "--exact", "--button", "right")
    step("rename source", "click", "--name", "Rename...", "--role", "menuItem", "--exact")
    step("type name", "fill", "--name", "Rename source", "--class", "juce::TextEditor", "--exact", "Hero triangle")
    step("commit name", "press", "Return", "--name", "Rename source", "--class", "juce::TextEditor", "--exact")
    names = [a.get("name") for a in saved().iter("asset")]
    assert names == ["Hero triangle"], names
    step("final", "screenshot", "--file", session.artifact_dir / "final.png")
    print("Keyframe lanes, key clipboard and source rename passed.", flush=True)
finally:
    session.stop_app()
