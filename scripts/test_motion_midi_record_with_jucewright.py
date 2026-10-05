#!/usr/bin/env python3
"""Record real CoreMIDI notes into a clip, then verify undo, save and cancel."""
import json
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

if sys.platform != "darwin":
    raise SystemExit("This external-input test requires macOS CoreMIDI.")
from jucewright_osci_browser.macos_midi import VirtualSource

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-midi-record")
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


def find(name):
    tree = json.loads(command("snapshot", "--json", "--full"))
    return next((node for node in nodes(tree) if node.get("componentName") == name or node.get("name") == name), None)


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="MIDI record verification", duration="8", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Record triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Recorded beam", kind="visual")
clip = ET.SubElement(track, "clip", id="3", asset="1", name="Triangle take", start="0", duration="8", offset="0", rate="1", timeBase="seconds", contentBpm="120")
for group, base in [("position", 0), ("rotation", 0), ("scale", 1)]:
    for axis in "xyz":
        ET.SubElement(clip, "property", name=group + "." + axis, base=str(base))
for name, base in [("red", .2), ("green", 1), ("blue", .35), ("weight", 1)]:
    ET.SubElement(clip, "property", name=name, base=str(base))
ET.SubElement(composition, "camera", id="5", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
fixture = session.artifact_dir / "midi-record.osci-motion"
fixture.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def saved_notes():
    step("save project", "press", "command + s", "--class", "MotionEditor")
    data = fixture.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    return saved.findall("./composition/track/clip/midi/note")


def wait_status(text, timeout=8.0):
    deadline = time.monotonic() + timeout
    value = None
    while time.monotonic() < deadline:
        node = find("MIDI recording status")
        value = node.get("value", "") if node is not None else None
        if value is not None and text in value:
            return value
        time.sleep(.1)
    raise RuntimeError("status never contained " + repr(text) + "; last " + repr(value))


source = None
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(fixture)
    command("wait-for-locator", "--name", "Recorded beam", "--class", "juce::Label", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    source = VirtualSource("Motion record test")
    command("wait", "--ms", "1500")
    step("open input settings", "select-option", "--role", "menuBar", "--text", "Settings...")
    device_list = "juce::CustomAudioDeviceSelectorComponent::CustomMidiInputSelectorComponentListBox"
    command("wait-for-locator", "--class", device_list, "--exact")
    step("select virtual MIDI input", "select-option", "--class", device_list, "--exact", "--index", str(source.source_index()))
    step("enable virtual MIDI input", "press", "Return", "--class", device_list, "--exact")
    step("close input settings", "click", "--name", "Close icon", "--exact")
    step("select clip", "click", "--class", "MotionTimelineView", "--position", "240,77")
    step("open notes", "click", "--name", "Notes", "--class", "osci::TabBar::Tab", "--exact")

    # Take 1: two notes, the second held by sustain past its note-off.
    step("start recording", "click", "--name", "Record notes", "--class", "juce::TextButton", "--exact")
    wait_status("Recording")
    source.send([0x90, 60, 100]); time.sleep(0.4); source.send([0x80, 60, 0])
    time.sleep(0.2)
    source.send([0xB0, 64, 127]); source.send([0x90, 67, 70]); time.sleep(0.2)
    source.send([0x80, 67, 0]); time.sleep(0.5); source.send([0xB0, 64, 0])
    step("recording screenshot", "screenshot", "--file", session.artifact_dir / "recording.png")
    step("stop recording", "click", "--name", "Stop recording", "--class", "juce::TextButton", "--exact")
    wait_status("Recorded 2 notes")
    command("wait-for-locator", "--name", "Undo Record MIDI notes", "--role", "label", "--exact")
    step("recorded screenshot", "screenshot", "--file", session.artifact_dir / "recorded.png")
    notes = saved_notes()
    assert len(notes) == 2, [n.attrib for n in notes]
    first, second = sorted(notes, key=lambda n: float(n.get("start")))
    assert first.get("pitch") == "60" and first.get("velocity") == "100", first.attrib
    assert second.get("pitch") == "67" and second.get("velocity") == "70", second.attrib
    # 400 ms at 120 bpm is 0.8 beats; device and CoreMIDI jitter stay well below 0.2 beats.
    assert abs(float(first.get("duration")) - .8) < .2, first.attrib
    assert float(second.get("duration")) > 1.1, "sustain must extend the second note: " + str(second.attrib)

    step("undo take", "click", "--name", "Undo", "--exact")
    assert not saved_notes(), "undo removes the whole take"
    step("redo take", "click", "--name", "Redo", "--exact")
    assert len(saved_notes()) == 2

    # Take 2 cancelled: existing notes must survive untouched.
    step("seek to start", "press", "Home", "--class", "MotionEditor")
    step("start cancelled take", "click", "--name", "Record notes", "--class", "juce::TextButton", "--exact")
    wait_status("Recording")
    source.send([0x90, 72, 90]); time.sleep(0.3); source.send([0x80, 72, 0])
    step("cancel take", "click", "--name", "Cancel", "--class", "juce::TextButton", "--exact")
    wait_status("cancelled")
    assert len(saved_notes()) == 2, "cancel keeps existing notes"

    # Take 3 merges with existing notes in one undo step.
    step("seek to start again", "press", "Home", "--class", "MotionEditor")
    step("start merge take", "click", "--name", "Record notes", "--class", "juce::TextButton", "--exact")
    wait_status("Recording")
    source.send([0x90, 64, 110]); time.sleep(0.3); source.send([0x80, 64, 0])
    step("stop merge take", "click", "--name", "Stop recording", "--class", "juce::TextButton", "--exact")
    wait_status("Recorded 1 note.")
    assert len(saved_notes()) == 3
    step("undo merge", "click", "--name", "Undo", "--exact")
    assert len(saved_notes()) == 2, "merge undo restores the previous take only"
    step("final screenshot", "screenshot", "--file", session.artifact_dir / "final.png")
    print("Real CoreMIDI record, sustain, undo/redo, cancel, merge and save passed.", flush=True)
finally:
    if source is not None:
        source.send([0xB0, 64, 0])
        source.close()
    session.stop_app()
