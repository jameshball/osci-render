#!/usr/bin/env python3
"""Exercise reusable composition authoring in the existing timeline workspace."""
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
    step("open clip menu", "click", "--class", "MotionTimelineView", "--position", "300,68", "--button", "right")
    step("create composition", "click", "--name", "Create composition from selection", "--role", "menuItem", "--exact")
    step("save created composition", "press", "command + s", "--class", "MotionEditor")
    created = read_project(seed)
    assert len(created.findall("./composition/definition")) == 1
    assert len(created.findall("./composition/track/clip[@composition]")) == 1
    assert len(created.findall("./composition/track")) == 1
    step("open composition", "click", "--class", "MotionTimelineView", "--position", "300,68", "--click-count", 2)
    command("wait-for-locator", "--name", "Back to Main", "--exact")
    step("inside composition", "screenshot", "--file", session.artifact_dir / "inside-composition.png")
    # Scope entry intentionally clears selection; select the child before duplicating.
    step("select child", "click", "--class", "MotionTimelineView", "--position", "300,68")
    step("duplicate selected child", "press", "command + d", "--class", "MotionTimelineView")
    step("save inside composition", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    assert len(saved.findall("./composition/track/clip[@composition]")) == 1
    assert len(saved.findall("./composition/definition/composition/track/clip")) == 2
    step("undo child duplication", "click", "--name", "Undo", "--exact")
    step("redo child duplication", "click", "--name", "Redo", "--exact")
    step("open composition name", "click", "--name", "Composition 1", "--class", "juce::Label", "--exact", "--click-count", 2)
    step("rename composition", "fill", "--name", "Composition name", "--class", "juce::TextEditor", "--exact", "Shared motif")
    step("commit composition name", "press", "Return")
    step("save renamed composition", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    assert saved.find("./composition/definition/composition").get("name") == "Shared motif"
    assert saved.find("composition").get("name") == "Nested rendering study"
    step("open child menu for nesting", "click", "--class", "MotionTimelineView", "--position", "300,68", "--button", "right")
    step("create inner composition", "click", "--name", "Create composition from selection", "--role", "menuItem", "--exact")
    step("save inner composition", "press", "command + s", "--class", "MotionEditor")
    assert len(read_project(seed).findall("./composition/definition")) == 2
    step("enter inner composition", "click", "--class", "MotionTimelineView", "--position", "300,68", "--click-count", 2)
    command("wait-for-locator", "--name", "Back to Shared motif", "--exact")
    step("nested editor", "screenshot", "--file", session.artifact_dir / "nested-editor.png")
    step("return to shared parent", "click", "--name", "Back to Shared motif", "--exact")
    command("wait-for-locator", "--name", "Back to Main", "--exact")
    step("return to main", "click", "--name", "Back to Main", "--exact")
    step("main after editing", "screenshot", "--file", session.artifact_dir / "main-after-editing.png")
    session.keep_app = False
    session.launch_app("nested-authoring-reopen")
    session.keep_app = keep
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(seed)
    command("wait", "--ms", 500)
    step("reopen saved composition", "click", "--class", "MotionTimelineView", "--position", "300,68", "--click-count", 2)
    command("wait-for-locator", "--name", "Shared motif", "--class", "juce::Label", "--exact")
    snapshot = command("snapshot", "--json", "--full")
    assert "Shared motif" in snapshot
    step("reopened shared editor", "screenshot", "--file", session.artifact_dir / "reopened-editor.png")
    print("Create, enter, edit, undo/redo, rename, return, save and reopen passed", flush=True)

finally:
    session.stop_app()
