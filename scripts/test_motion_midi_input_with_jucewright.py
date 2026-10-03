#!/usr/bin/env python3
"""Track MIDI input arming, channel choice and a MIDI controller modulator through the real workspace."""
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
session.launch_app("motion-midi-input")
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
composition = ET.SubElement(root, "composition", name="Input study", duration="8", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Keys", kind="visual")
clip = ET.SubElement(track, "clip", id="3", asset="1", name="Lead", start="0", duration="8", offset="0", rate="1", timeBase="seconds", contentBpm="120")
ET.SubElement(clip, "instrument", attack="0", decay="0", sustain="1", release="0.1")
midi = ET.SubElement(clip, "midi", asset="0")
ET.SubElement(midi, "note", id="1", start="0", duration="4", pitch="60", velocity="100", channel="1")
ET.SubElement(midi, "control", beat="2", channel="1", number="1", value="100")
for group, base in (("position", 0), ("rotation", 0), ("scale", 1)):
    for axis in "xyz":
        ET.SubElement(clip, "property", name=f"{group}.{axis}", base=str(base))
for prop, base in (("red", 1), ("green", 1), ("blue", 1), ("weight", 1)):
    ET.SubElement(clip, "property", name=prop, base=str(base))
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "input.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def saved():
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Input study", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    step("arm", "click", "--name", "MIDI input track 2", "--exact")
    wait_undo("Arm MIDI input")
    assert saved().find("track").get("midiInput") == "17"
    step("track menu", "click", "--name", "Reorder track 2", "--exact")
    step("input submenu", "click", "--name", "MIDI input", "--role", "menuItem", "--exact")
    step("channel 3", "click", "--name", "Channel 3", "--role", "menuItem", "--exact")
    tree = saved()
    assert tree.find("track").get("midiInput") == "3", tree.find("track").attrib
    assert tree.find("track/clip/midi/control") is not None, "controller data survives save"
    step("armed", "screenshot", "--file", session.artifact_dir / "armed.png")
    # A controller modulator following the clip's mod wheel.
    step("modulators", "click", "--name", "Modulators", "--class", "MotionTabs::Tab", "--exact")
    step("add modulator", "click", "--name", "Add modulator", "--exact")
    step("add cc", "click", "--name", "MIDI controller", "--role", "menuItem", "--exact")
    wait_undo("Add modulator")
    step("source", "select-option", "--name", "Envelope source", "--text", "Lead")
    step("pitch bend", "select-option", "--name", "Modulator controller", "--text", "Pitch bend")
    modulator = saved().find("modulator")
    assert modulator.get("kind") == "controller" and modulator.get("source") == "3" and modulator.get("controller") == "128", modulator.attrib
    step("disarm", "click", "--name", "MIDI input track 2", "--exact")
    wait_undo("Disarm MIDI input")
    assert saved().find("track").get("midiInput") is None
    print("MIDI input arming, channel and controller modulator passed.", flush=True)
finally:
    session.stop_app()
