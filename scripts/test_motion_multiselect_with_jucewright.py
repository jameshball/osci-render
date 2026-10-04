#!/usr/bin/env python3
"""Verify multi-clip selection, movement, deletion and undo through the UI.

This is a usability stress pass, not the finished music-video benchmark.
"""
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
session.launch_app("motion-multiselect")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Multi-clip selection study", duration="180", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "multiselect.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

def saved_clips():
    step("save selection test", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).findall("./composition/track/clip")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    for index in range(2):
        source = session.artifact_dir / f"Selection {index}.svg"
        source.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M10 10 L90 10 L50 90 Z"/></svg>')
        step("import selection source", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
        command("wait-for-locator", "--name", source.name, "--class", "juce::Label", "--exact")
    step("select first clip", "click", "--class", "MotionTimelineView", "--position", "280,68")
    step("extend selection", "click", "--class", "MotionTimelineView", "--position", "280,108", "--modifiers", "shift")
    step("move selected clips", "drag", "--class", "MotionTimelineView", "--position", "280,68", "--dx", "140", "--dy", "0", "--steps", "12")
    clips = saved_clips()
    assert len(clips) == 2 and all(float(c.get("start")) == 2 for c in clips), [c.attrib for c in clips]
    step("reject move above first track", "drag", "--class", "MotionTimelineView", "--position", "420,108", "--dx", "0", "--dy", "-40", "--steps", "8")
    assert all(float(c.get("start")) == 2 for c in saved_clips())
    step("selection moved", "screenshot", "--file", session.artifact_dir / "selection-moved.png")
    step("undo both moves", "click", "--name", "Undo", "--exact")
    assert all(float(c.get("start")) == 0 for c in saved_clips())
    step("delete selected clips", "press", "Backspace", "--class", "MotionTimelineView")
    assert not saved_clips()
    step("undo both deletions", "click", "--name", "Undo", "--exact")
    assert len(saved_clips()) == 2
    step("select originals for duplication", "click", "--class", "MotionTimelineView", "--position", "280,68")
    step("extend duplicate selection", "click", "--class", "MotionTimelineView", "--position", "280,108", "--modifiers", "shift")
    original_ids = {c.get("id") for c in saved_clips()}
    step("duplicate selection", "press", "command + d", "--class", "MotionTimelineView")
    duplicated = saved_clips()
    assert len(duplicated) == 4
    copies = [c for c in duplicated if c.get("id") not in original_ids]
    assert len(copies) == 2 and all(float(c.get("start")) == 5 for c in copies)
    step("duplicated selection", "screenshot", "--file", session.artifact_dir / "selection-duplicated.png")
    step("delete just the copies", "press", "Backspace", "--class", "MotionTimelineView")
    assert {c.get("id") for c in saved_clips()} == original_ids
    step("undo copy deletion", "click", "--name", "Undo", "--exact")
    assert len(saved_clips()) == 4
    step("undo selection duplication", "click", "--name", "Undo", "--exact")
    assert {c.get("id") for c in saved_clips()} == original_ids
    print("Multi-clip move/duplicate/delete/undo UI workflow passed", flush=True)
finally:
    session.stop_app()
