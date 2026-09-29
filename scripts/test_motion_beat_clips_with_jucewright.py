#!/usr/bin/env python3
"""Exercise beat/seconds clip placement, tempo conflict rejection and splitting."""
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
session.launch_app("motion-beat-clips")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def find(value, name):
    if isinstance(value, dict):
        if value.get("componentName") == name:
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


def tempo(value):
    tree = json.loads(command("snapshot", "--json", "--full"))
    target = find(tree, "Project tempo")
    step("set tempo " + str(value), "fill", target["ref"], str(value))
    step("commit tempo", "press", "Return")


# JUCE MemoryBlock's length-prefixed little-bit-order encoding, used by projects.
def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Beat timing verification", duration="20", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Timing triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Mixed timing", kind="visual")
for identity, name, start, duration, domain in [(3, "Musical clip", 4, 8, "beats"), (4, "Absolute clip", 8, 2, "seconds")]:
    clip = ET.SubElement(track, "clip", id=str(identity), asset="1", name=name, start=str(start), duration=str(duration), offset="0", rate="1", timeBase=domain, contentBpm="120")
    for group, base in [("position", 0), ("rotation", 0), ("scale", 1)]:
        for axis in "xyz":
            ET.SubElement(clip, "property", name=group + "." + axis, base=str(base))
    for name, base in [("red", .2), ("green", 1), ("blue", .35), ("weight", 1)]:
        ET.SubElement(clip, "property", name=name, base=str(base))
ET.SubElement(composition, "camera", id="5", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
fixture = session.artifact_dir / "beat-clips.osci-motion"
fixture.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(fixture)], check=True)
    command("wait-for-locator", "--name", "Mixed timing", "--class", "juce::Label", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("original musical placement", "screenshot", "--file", session.artifact_dir / "120-bpm.png")
    tempo(240)
    command("wait-for-locator", "--name", "Undo Change tempo", "--role", "label", "--exact")
    step("faster musical placement", "screenshot", "--file", session.artifact_dir / "240-bpm.png")
    tempo(60)
    command("wait-for-locator", "--name", "Cannot change tempo", "--exact")
    step("overlap explanation", "screenshot", "--file", session.artifact_dir / "tempo-conflict.png")
    step("dismiss conflict", "press", "Escape")
    command("wait", "--ms", 600)
    command("wait-for-locator", "--name", "240.0", "--class", "juce::Label", "--exact")
    step("select musical clip", "click", "--class", "MotionTimelineView", "--position", "310,55")
    step("seek musical midpoint", "click", "--class", "MotionTimelineView", "--position", "310,12")
    step("split musical clip", "press", "command + k", "--class", "MotionTimelineView")
    command("wait-for-locator", "--name", "Undo Split clip", "--role", "label", "--exact")
    step("split result", "screenshot", "--file", session.artifact_dir / "split.png")
    step("undo split", "click", "--name", "Undo", "--exact")
    command("wait-for-locator", "--name", "Undo Change tempo", "--role", "label", "--exact")
    step("undo tempo", "click", "--name", "Undo", "--exact")
    command("wait-for-locator", "--name", "120.0", "--class", "juce::Label", "--exact")
    step("restored placement", "screenshot", "--file", session.artifact_dir / "restored.png")
    print("Beat clip timing workflow passed.", flush=True)
finally:
    session.stop_app()
