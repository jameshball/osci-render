#!/usr/bin/env python3
"""Exercise prepared MIDI playback, frozen audition, note gaps and tempo rebuilds."""
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
session.launch_app("motion-midi-playback")
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
composition = ET.SubElement(root, "composition", name="MIDI beam verification", duration="8", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="MIDI triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Musical beam", kind="visual")
clip = ET.SubElement(track, "clip", id="3", asset="1", name="Triangle notes", start="0", duration="16", offset="0", rate="1", timeBase="beats", contentBpm="120")
for group, base in [("position", 0), ("rotation", 0), ("scale", 1)]:
    for axis in "xyz":
        ET.SubElement(clip, "property", name=group + "." + axis, base=str(base))
for name, base in [("red", .2), ("green", 1), ("blue", .35), ("weight", 1)]:
    ET.SubElement(clip, "property", name=name, base=str(base))
pattern = ET.SubElement(clip, "midi", asset="0")
for identity, beat, pitch in [(1, 0, 60), (2, 2, 67), (3, 4, 72)]:
    ET.SubElement(pattern, "note", id=str(identity), start=str(beat), duration="1", pitch=str(pitch), velocity="127", channel="1")
ET.SubElement(composition, "camera", id="5", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
fixture = session.artifact_dir / "midi-beam.osci-motion"
fixture.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(fixture)
    command("wait-for-locator", "--name", "Musical beam", "--class", "juce::Label", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("select MIDI clip", "click", "--class", "MotionTimelineView", "--position", "240,77")
    step("seek held note", "click", "--class", "MotionTimelineView", "--position", "240,12")
    command("wait", "--ms", 1000)
    step("frozen note audition", "screenshot", "--file", session.artifact_dir / "held-note.png")
    step("seek note gap", "click", "--class", "MotionTimelineView", "--position", "340,12")
    command("wait", "--ms", 1000)
    step("gap is silent", "screenshot", "--file", session.artifact_dir / "note-gap.png")
    step("seek later note", "click", "--class", "MotionTimelineView", "--position", "380,12")
    command("wait", "--ms", 1000)
    step("later note audition", "screenshot", "--file", session.artifact_dir / "later-note.png")
    step("play MIDI timeline", "click", "--name", "Play", "--exact")
    command("wait", "--ms", 300)
    step("pause MIDI timeline", "click", "--name", "Pause", "--exact")
    tempo(240)
    command("wait", "--ms", 1000)
    step("tempo rebuild", "screenshot", "--file", session.artifact_dir / "tempo.png")
    step("undo tempo", "click", "--name", "Undo", "--exact")
    command("wait-for-locator", "--name", "120.0", "--class", "juce::Label", "--exact")
    print("MIDI playback workflow passed; inspect held-note/gap screenshots against the playhead.", flush=True)
finally:
    session.stop_app()
