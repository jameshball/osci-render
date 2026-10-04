#!/usr/bin/env python3
"""Author, drag, navigate, edit and persist composition markers through Motion."""
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
session.launch_app("motion-markers")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def edit(name, value):
    step("edit " + name, "fill", "--name", name, "--class", "juce::TextEditor", "--exact", value)


def save():
    step("save marker project", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


def markers():
    return {item.get("name"): float(item.get("time")) for item in save().findall("marker")}


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Marker study", duration="180", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "markers.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Marker study", "--timeout-ms", 10000)
    step("size workspace", "resize-window", "--w", 1440, "--h", 900)
    source = session.artifact_dir / "Title.txt"
    source.write_text("MOTION")
    step("import source", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Undo Import object", "--role", "label", "--exact")
    step("ruler menu", "click", "--class", "MotionTimelineView", "--position", "360,12", "--button", "right")
    step("add ruler marker", "click", "--name", "Add marker here...", "--role", "menuItem", "--exact")
    edit("Marker name", "Verse")
    edit("Marker position", "2s")
    step("save verse marker", "click", "--name", "Save marker", "--exact")
    command("wait-for-locator", "--name", "Undo Add marker", "--role", "label", "--exact")
    assert markers() == {"Verse": 2}
    step("marker shortcut", "press", "m", "--class", "MotionTimelineView")
    edit("Marker name", "Chorus")
    edit("Marker position", "6s")
    step("save chorus marker", "click", "--name", "Save marker", "--exact")
    assert markers() == {"Verse": 2, "Chorus": 6}
    step("drag verse one second", "drag", "--class", "MotionTimelineView", "--position", "370,36", "--dx", 70, "--dy", 0, "--steps", 12)
    assert markers() == {"Verse": 3, "Chorus": 6}
    step("undo marker drag", "click", "--name", "Undo", "--exact")
    assert markers()["Verse"] == 2
    step("redo marker drag", "click", "--name", "Redo", "--exact")
    assert markers()["Verse"] == 3
    step("jump to verse", "click", "--class", "MotionTimelineView", "--position", "450,36")
    command("wait-for-value", "--component-name", "Timeline position", "--value", "3.000s", "--timeout-ms", 5000)
    step("next marker shortcut", "press", "k", "--class", "MotionTimelineView")
    command("wait-for-value", "--component-name", "Timeline position", "--value", "6.000s", "--timeout-ms", 5000)
    step("previous marker shortcut", "press", "j", "--class", "MotionTimelineView")
    command("wait-for-value", "--component-name", "Timeline position", "--value", "3.000s", "--timeout-ms", 5000)
    step("select verse marker", "click", "--class", "MotionTimelineView", "--position", "450,36")
    step("delete selected marker", "press", "backspace", "--class", "MotionTimelineView")
    assert markers() == {"Chorus": 6}
    assert len(save().findall("track/clip")) == 1
    step("undo marker deletion", "click", "--name", "Undo", "--exact")
    assert len(markers()) == 2
    step("select clip before marker", "click", "--class", "MotionTimelineView", "--position", "300,86")
    step("select marker exclusively", "click", "--class", "MotionTimelineView", "--position", "450,36")
    step("duplicate shortcut with marker selected", "press", "command + d", "--class", "MotionTimelineView")
    assert len(save().findall("track/clip")) == 1
    step("select clip from preview", "click", "--class", "MotionCompositionView", "--position", "60,235")
    step("delete externally selected clip", "press", "backspace", "--class", "MotionTimelineView")
    state = save()
    assert len(state.findall("track/clip")) == 0
    assert len(state.findall("marker")) == 2
    step("undo clip deletion", "click", "--name", "Undo", "--exact")
    assert len(save().findall("track/clip")) == 1
    step("chorus menu", "click", "--class", "MotionTimelineView", "--position", "660,36", "--button", "right")
    step("edit chorus", "click", "--name", "Edit marker...", "--role", "menuItem", "--exact")
    edit("Marker name", "Final chorus")
    edit("Marker position", "3s")
    step("reject occupied position", "click", "--name", "Save marker", "--exact")
    command("wait-for-locator", "--name", "A marker already exists at this position.", "--role", "label", "--exact")
    edit("Marker position", "5s")
    step("save edited chorus", "click", "--name", "Save marker", "--exact")
    assert markers() == {"Verse": 3, "Final chorus": 5}
    step("marker lane screenshot", "screenshot", "--file", session.artifact_dir / "markers.png")
    session.keep_app = False
    session.launch_app("markers-reopen")
    session.keep_app = keep
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Marker study", "--timeout-ms", 10000)
    assert markers() == {"Verse": 3, "Final chorus": 5}
    step("reopened markers", "screenshot", "--file", session.artifact_dir / "markers-reopened.png")
    print("Marker add/drag/navigation/delete/edit/rejection/undo and reopen passed", flush=True)
finally:
    session.stop_app()
