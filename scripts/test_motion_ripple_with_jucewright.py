#!/usr/bin/env python3
"""Verify explicit per-track ripple deletion and ordinary gap-preserving deletion."""
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
session.launch_app("motion-ripple")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Ripple study", duration="180", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "ripple.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

def saved_clips():
    step("save selection test", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).findall("./composition/track/clip")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Ripple study", "--timeout-ms", "10000")
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    for index in range(2):
        source = session.artifact_dir / f"Selection {index}.svg"
        source.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M10 10 L90 10 L50 90 Z"/></svg>')
        step("import selection source", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
        command("wait-for-locator", "--name", source.name, "--class", "juce::Label", "--exact")
    step("select first clip", "click", "--class", "MotionTimelineView", "--position", "230,46")
    step("duplicate at five seconds", "press", "command + d", "--class", "MotionTimelineView")
    step("duplicate at ten seconds", "press", "command + d", "--class", "MotionTimelineView")
    original = {c.get("id"): float(c.get("start")) for c in saved_clips()}
    assert sorted(original.values()) == [0, 0, 5, 10], original
    step("select middle clip", "click", "--class", "MotionTimelineView", "--position", "600,46")
    step("delete leaving gap", "press", "Backspace", "--class", "MotionTimelineView")
    assert sorted(float(c.get("start")) for c in saved_clips()) == [0, 0, 10]
    step("undo gap deletion", "click", "--name", "Undo", "--exact")
    step("reselect middle clip", "click", "--class", "MotionTimelineView", "--position", "600,46")
    step("open ripple menu", "click", "--class", "MotionTimelineView", "--position", "600,46", "--button", "right")
    step("ripple command screenshot", "screenshot", "--file", session.artifact_dir / "ripple-menu.png")
    step("ripple delete middle", "click", "--name", "Ripple delete on selected tracks (Shift+Delete)", "--role", "menuItem", "--exact")
    assert sorted(float(c.get("start")) for c in saved_clips()) == [0, 0, 5]
    step("ripple result", "screenshot", "--file", session.artifact_dir / "ripple-result.png")
    step("undo ripple", "click", "--name", "Undo", "--exact")
    assert {c.get("id"): float(c.get("start")) for c in saved_clips()} == original
    step("redo ripple", "click", "--name", "Redo", "--exact")
    step("select first track head", "click", "--class", "MotionTimelineView", "--position", "230,46")
    step("select second track head", "click", "--class", "MotionTimelineView", "--position", "230,86", "--modifiers", "shift")
    step("ripple shortcut across tracks", "press", "shift + Backspace", "--class", "MotionTimelineView")
    clips = saved_clips()
    assert len(clips) == 1 and float(clips[0].get("start")) == 0
    step("undo collective ripple", "click", "--name", "Undo", "--exact")
    assert sorted(float(c.get("start")) for c in saved_clips()) == [0, 0, 5]
    session.stop_app()
    session.keep_app = False
    session.launch_app("ripple-reopened")
    session.keep_app = keep
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Ripple study", "--timeout-ms", "10000")
    assert sorted(float(c.get("start")) for c in saved_clips()) == [0, 0, 5]
    step("reopened ripple", "screenshot", "--file", session.artifact_dir / "ripple-reopened.png")
    print("Ripple and ordinary deletion, per-track scope, undo/redo, keyboard and reopen passed", flush=True)
finally:
    session.stop_app()
