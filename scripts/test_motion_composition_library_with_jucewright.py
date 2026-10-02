#!/usr/bin/env python3
"""Verify composition library insertion, opening and independent copies."""
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
    step("open clip menu", "click", "--class", "MotionTimelineView", "--position", "250,46", "--button", "right")
    step("create composition", "click", "--name", "Create composition from selection", "--role", "menuItem", "--exact")
    step("save created composition", "press", "command + s", "--class", "MotionEditor")
    created = read_project(seed)
    assert len(created.findall("./composition/definition")) == 1
    assert len(created.findall("./composition/track/clip[@composition]")) == 1
    assert len(created.findall("./composition/track")) == 1
    step("open composition", "click", "--class", "MotionTimelineView", "--position", "250,46", "--click-count", 2)
    command("wait-for-locator", "--name", "Back to Main", "--exact")
    step("inside composition", "screenshot", "--file", session.artifact_dir / "inside-composition.png")
    # Scope entry intentionally clears selection; select the child before duplicating.
    step("select child", "click", "--class", "MotionTimelineView", "--position", "250,46")
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
    step("open child menu for nesting", "click", "--class", "MotionTimelineView", "--position", "250,46", "--button", "right")
    step("create inner composition", "click", "--name", "Create composition from selection", "--role", "menuItem", "--exact")
    step("save inner composition", "press", "command + s", "--class", "MotionEditor")
    assert len(read_project(seed).findall("./composition/definition")) == 2
    step("enter inner composition", "click", "--class", "MotionTimelineView", "--position", "250,46", "--click-count", 2)
    command("wait-for-locator", "--name", "Back to Shared motif", "--exact")
    step("nested editor", "screenshot", "--file", session.artifact_dir / "nested-editor.png")
    step("return to shared parent", "click", "--name", "Back to Shared motif", "--exact")
    command("wait-for-locator", "--name", "Back to Main", "--exact")
    step("return to main", "click", "--name", "Back to Main", "--exact")
    step("duplicate main instance", "press", "command + d", "--class", "MotionTimelineView")
    step("open duplicate menu", "click", "--class", "MotionTimelineView", "--position", "600,46", "--button", "right")
    step("make duplicate independent", "click", "--name", "Make composition unique", "--role", "menuItem", "--exact")
    step("save independent instance", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    instances = saved.findall("./composition/track/clip[@composition]")
    assert len(instances) == 2 and instances[0].get("composition") != instances[1].get("composition")
    original_id, copy_id = [item.get("composition") for item in instances]
    original = saved.find(f"./composition/definition[@id='{original_id}']/composition")
    independent = saved.find(f"./composition/definition[@id='{copy_id}']/composition")
    assert original.get("name") == "Shared motif" and independent.get("name") == "Shared motif copy"
    assert original.find("track/clip[@composition]").get("composition") == independent.find("track/clip[@composition]").get("composition")
    step("open independent instance", "click", "--class", "MotionTimelineView", "--position", "600,46", "--click-count", 2)
    command("wait-for-locator", "--name", "Shared motif copy", "--class", "juce::Label", "--exact")
    step("open independent name", "click", "--name", "Shared motif copy", "--class", "juce::Label", "--exact", "--click-count", 2)
    step("rename independent definition", "fill", "--name", "Composition name", "--class", "juce::TextEditor", "--exact", "Independent motif")
    step("commit independent name", "press", "Return")
    step("save independent edit", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    assert saved.find(f"./composition/definition[@id='{original_id}']/composition").get("name") == "Shared motif"
    assert saved.find(f"./composition/definition[@id='{copy_id}']/composition").get("name") == "Independent motif"
    step("return from independent editor", "click", "--name", "Back to Main", "--exact")
    step("select original in library", "click", "--name", "Shared motif", "--role", "listItem", "--exact", "--position", "6,12")
    step("open library definition", "click", "--name", "Open composition", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Shared motif", "--class", "juce::Label", "--exact")
    before_rejected = ET.tostring(saved.find(f"./composition/definition[@id='{original_id}']/composition"))
    step("reject self insertion", "click", "--name", "Shared motif", "--role", "listItem", "--exact")
    step("dismiss recursive insertion error", "click", "--name", "OK", "--class", "juce::TextButton", "--exact")
    step("save after rejected insertion", "press", "command + s", "--class", "MotionEditor")
    assert ET.tostring(read_project(seed).find(f"./composition/definition[@id='{original_id}']/composition")) == before_rejected
    step("return from library editor", "click", "--name", "Back to Main", "--exact")
    step("insert original from library", "click", "--name", "Shared motif", "--role", "listItem", "--exact")
    step("save library insertion", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    instances = saved.findall("./composition/track/clip[@composition]")
    assert len(instances) == 3 and sum(item.get("composition") == original_id for item in instances) == 2
    tree = json.loads(command("snapshot", "--json", "--full"))
    def find(value, class_name, name=None):
        if isinstance(value, dict):
            if value.get("class") == class_name and (name is None or value.get("name") == name):
                return value
            for child in value.values():
                found = find(child, class_name, name)
                if found is not None:
                    return found
        elif isinstance(value, list):
            for child in value:
                found = find(child, class_name, name)
                if found is not None:
                    return found
        return None
    source_row = find(tree, "juce::ListBox::RowComponent", "Independent motif")
    destination = find(tree, "MotionTimelineView")
    assert source_row is not None and destination is not None
    start, finish = source_row["bounds"], destination["bounds"]
    dx = finish["x"] + 800 - (start["x"] + 12)
    dy = finish["y"] + 126 - (start["y"] + 12)
    step("drag independent composition", "drag", source_row["ref"], "--position", "12,12", "--dx", str(dx), "--dy", str(dy), "--steps", "24")
    step("save dragged composition", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(seed)
    instances = saved.findall("./composition/track/clip[@composition]")
    assert len(instances) == 4 and sum(item.get("composition") == copy_id for item in instances) == 2
    step("composition library", "screenshot", "--file", session.artifact_dir / "composition-library.png")
    session.keep_app = False
    session.launch_app("composition-library-reopen")
    session.keep_app = keep
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(seed)
    command("wait-for-locator", "--name", "Independent motif", "--role", "listItem", "--exact")
    step("reopened library", "screenshot", "--file", session.artifact_dir / "reopened-library.png")
    print("Composition copy isolation, library open/insert, cycle rejection and reopen passed", flush=True)

finally:
    session.stop_app()
