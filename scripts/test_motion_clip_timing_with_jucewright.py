#!/usr/bin/env python3
"""Verify precise timing, rejection, undo and selection through Motion's UI."""
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
session.launch_app("motion-clip-timing")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def find(value, name):
    if isinstance(value, dict):
        if value.get("componentName") == name or value.get("class") == name:
            return value
        for child in value.values():
            result = find(child, name)
            if result is not None:
                return result
    elif isinstance(value, list):
        for child in value:
            result = find(child, name)
            if result is not None:
                return result
    return None


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Precision timing", duration="180", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Timing verification", kind="visual")
for id_, start in [(3, 0), (4, 10)]:
    clip = ET.SubElement(track, "clip", id=str(id_), asset="1", name="First clip" if id_ == 3 else "Second clip", start=str(start), duration="5", offset="0", rate="1", timeBase="seconds", contentBpm="120")
    for group, base in [("position", 0), ("rotation", 0), ("scale", 1)]:
        for axis in "xyz":
            ET.SubElement(clip, "property", name=group + "." + axis, base=str(base))
    for name, base in [("red", .2), ("green", 1), ("blue", .35), ("weight", 1)]:
        ET.SubElement(clip, "property", name=name, base=str(base))
ET.SubElement(composition, "camera", id="5", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "precision-timing.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def saved():
    step("save timing project", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    result = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    clips = {n.get("id"): n for n in result.findall("./composition/track/clip")}
    return result, clips


def edit(name, value):
    target = find(json.loads(command("snapshot", "--json", "--full")), name)
    assert target is not None, name
    step("open " + name, "click", target["ref"], "--click-count", "2")
    field = find(json.loads(command("snapshot", "--json", "--full")), name)
    editor = find(field, "juce::TextEditor")
    assert editor is not None, "Numeric field did not open its editor: " + name
    step("set " + name, "fill", editor["ref"], str(value))
    step("commit " + name, "press", "Return")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-locator", "--name", "Timing verification", "--class", "juce::Label", "--exact")
    step("size timing workspace", "resize-window", "--w", "1440", "--h", "900")
    step("select first clip", "click", "--class", "MotionTimelineView", "--position", "250,46")
    step("open clip inspector", "click", "--name", "Clip", "--class", "osci::TabBar::Tab", "--exact")
    edit("Clip duration", 8)
    _, clips = saved()
    assert float(clips['3'].get('duration')) == 8
    edit("Clip start", 3)
    command("wait-for-locator", "--name", "The requested timing is invalid or overlaps another clip on this track.", "--exact")
    _, clips = saved()
    assert float(clips['3'].get('start')) == 0
    edit("Clip start", 2)
    edit("Clip source offset", 1.25)
    edit("Clip speed", 2)
    _, clips = saved()
    assert [float(clips['3'].get(key)) for key in ('start', 'duration', 'offset', 'rate')] == [2, 8, 1.25, 2]
    step("undo speed change", "click", "--name", "Undo", "--exact")
    _, clips = saved()
    assert float(clips['3'].get('rate')) == 1
    step("redo speed change", "click", "--name", "Redo", "--exact")
    edit("Clip speed", 0)
    _, clips = saved()
    assert float(clips['3'].get('rate')) == 2
    edit("Clip source offset", "invalid")
    command("wait-for-locator", "--name", "Enter a finite number.", "--exact")
    _, clips = saved()
    assert float(clips['3'].get('offset')) == 1.25
    step("select second clip", "click", "--class", "MotionTimelineView", "--position", "920,46")
    # The Clip tab must remain open while changing the selected timeline clip.
    command("wait-for-locator", "--name", "Clip timing inspector", "--exact")
    edit("Clip duration", 180)
    result, clips = saved()
    assert float(clips['4'].get('duration')) == 180
    assert float(result.find('composition').get('duration')) == 190
    assert float(clips['3'].get('duration')) == 8
    step("preview long clip", "click", "--class", "MotionTimelineView", "--position", "1010,12")
    step("timing inspector", "screenshot", "--file", session.artifact_dir / "timing.png")
    step("compact timing workspace", "resize-window", "--w", "1100", "--h", "700")
    step("compact timing inspector", "screenshot", "--file", session.artifact_dir / "compact.png")
    step("undo long duration", "click", "--name", "Undo", "--exact")
    result, clips = saved()
    assert float(clips['4'].get('duration')) == 5
    assert float(result.find('composition').get('duration')) == 180
    print("Clip timing UI/save/rejection/undo workflow passed.", flush=True)
finally:
    session.stop_app()
