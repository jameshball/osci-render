#!/usr/bin/env python3
"""Exercise MIDI note authoring within shared composition definitions."""
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
session.launch_app("motion-nested-render")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def write_project(root, path):
    xml = ET.tostring(root, encoding="utf-8")
    path.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def read_project(path):
    data = path.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    root = ET.Element("motion-project", schema="1")
    ET.SubElement(root, "composition", name="Nested rendering study", duration="20", bpm="120", fps="30")
    seed = session.artifact_dir / "seed.osci-motion"
    write_project(root, seed)
    session.open_project(seed)
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    source = session.artifact_dir / "Motif.txt"
    source.write_text("RETURN")
    step("import motif", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Motif.txt", "--class", "juce::Label", "--exact")
    step("save source", "press", "command + s", "--class", "MotionEditor")
    step("open clip menu", "click", "--class", "MotionTimelineView", "--position", "250,68", "--button", "right")
    step("create composition", "click", "--name", "Create composition from selection", "--role", "menuItem", "--exact")
    step("save created composition", "press", "command + s", "--class", "MotionEditor")
    created = read_project(seed)
    assert len(created.findall("./composition/definition")) == 1
    assert len(created.findall("./composition/track/clip[@composition]")) == 1
    assert len(created.findall("./composition/track")) == 1
    step("open composition", "click", "--class", "MotionTimelineView", "--position", "250,68", "--click-count", 2)
    command("wait-for-locator", "--name", "Back to Main", "--exact")
    step("select child", "click", "--class", "MotionTimelineView", "--position", "250,68")
    step("open notes", "click", "--name", "Notes", "--class", "MotionTabs::Tab", "--exact")
    # Give the piano roll the room it used to take for itself.
    step("taller notes", "drag", "--name", "Resize timeline", "--class", "osci::PanelDivider", "--exact", "--position", "20,3", "--dx", 0, "--dy", -64)
    step("create child pattern", "click", "--name", "Create notes", "--exact")
    step("draw child note", "click", "--class", "MotionNotesEditor", "--position", "222,170", "--click-count", 2)
    command("wait-for-locator", "--name", "Undo Add MIDI note", "--role", "label", "--exact")
    step("save scoped notes", "press", "command + s", "--class", "MotionEditor")
    notes_path = "./composition/definition/composition/track/clip/midi/note"
    saved = read_project(seed)
    authored = saved.findall(notes_path)
    assert len(authored) == 1
    assert not saved.findall("./composition/track/clip/midi"), "Pattern leaked onto the parent instance"
    step("notes in child", "screenshot", "--file", session.artifact_dir / "child-notes.png")
    step("return to main", "click", "--name", "Back to Main", "--exact")
    step("undo child note from main", "click", "--name", "Undo", "--exact")
    step("save undone notes", "press", "command + s", "--class", "MotionEditor")
    assert not read_project(seed).findall(notes_path)
    step("redo child note from main", "click", "--name", "Redo", "--exact")
    step("save redone notes", "press", "command + s", "--class", "MotionEditor")
    assert read_project(seed).find(notes_path).attrib == authored[0].attrib
    step("duplicate musical instance", "press", "command + d", "--class", "MotionTimelineView")
    step("save repeated composition", "press", "command + s", "--class", "MotionEditor")
    assert len(read_project(seed).findall("./composition/track/clip[@composition]")) == 2
    session.keep_app = False
    session.launch_app("nested-notes-reopen")
    session.keep_app = keep
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(seed)
    command("wait-for-locator", "--name", "Composition 1", "--class", "juce::Label", "--exact")
    step("reopen musical composition", "click", "--class", "MotionTimelineView", "--position", "250,68", "--click-count", 2)
    command("wait-for-locator", "--name", "Back to Main", "--exact")
    step("select reopened child", "click", "--class", "MotionTimelineView", "--position", "250,68")
    step("show reopened notes", "click", "--name", "Notes", "--class", "MotionTabs::Tab", "--exact")
    command("wait-for-locator", "--name", "Remove MIDI", "--exact")
    step("select child notes", "press", "command + a", "--class", "MotionNotesEditor")
    step("nudge shared pattern", "press", "Right", "--class", "MotionNotesEditor")
    step("save shared pattern edit", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    assert float(saved.find(notes_path).get("start")) > float(authored[0].get("start"))
    refs = saved.findall("./composition/track/clip[@composition]")
    assert len(refs) == 2 and refs[0].get("composition") == refs[1].get("composition")
    step("reopened shared notes", "screenshot", "--file", session.artifact_dir / "shared-notes.png")
    print("Nested note authoring, parent undo/redo, shared instances and fresh-process reopen passed", flush=True)

finally:
    session.stop_app()
