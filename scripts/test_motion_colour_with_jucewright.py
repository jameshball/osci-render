#!/usr/bin/env python3
"""Verify an animated beam-colour effect through the UI.

Retains neutral and hue-shifted visualiser screenshots and the saved project.
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
session.launch_app("motion-colour")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Colour effect study", duration="180", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "colour.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

def saved_clips():
    step("save selection test", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).findall("./composition/track/clip")


def find(value, name):
    if isinstance(value, dict):
        if value.get("componentName") == name or value.get("class") == name:
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


def edit(name, value):
    target = find(json.loads(command("snapshot", "--json", "--full")), name)
    assert target is not None, name
    step("open " + name, "click", target["ref"], "--click-count", 2)
    target = find(json.loads(command("snapshot", "--json", "--full")), name)
    editor = find(target, "juce::TextEditor")
    assert editor is not None, "Editor missing: " + name
    step("set " + name, "fill", editor["ref"], str(value))
    step("commit " + name, "press", "Return")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    source = session.artifact_dir / "Colour triangle.obj"
    source.write_text("v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
    step("import colour source", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", source.name, "--class", "juce::Label", "--exact")
    library = find(json.loads(command("snapshot", "--json", "--full")), "Library tabs")
    def effect_tab(tree):
        if isinstance(tree, dict):
            if tree.get("class") == "MotionTabs::Tab" and tree.get("name") == "Effects": return tree
            for value in tree.values():
                found = effect_tab(value)
                if found: return found
        elif isinstance(tree, list):
            for value in tree:
                found = effect_tab(value)
                if found: return found
    step("open effects library", "click", effect_tab(library)["ref"])
    def tiles(value):
        if isinstance(value, dict):
            if str(value.get("class", "")).endswith("Tile") and value.get("componentName") == "Colour":
                yield value
            for child in value.values():
                yield from tiles(child)
        elif isinstance(value, list):
            for child in value:
                yield from tiles(child)
    tile = next(tiles(json.loads(command("snapshot", "--json", "--full"))))
    step("add colour effect", "click", tile["ref"], "--click-count", 2)
    command("wait-for-locator", "--name", "Effect hue", "--role", "slider", "--exact")
    step("key initial hue", "click", "--name", "Key effect hue", "--exact")
    command("wait", "--ms", 1000)
    step("neutral colour", "screenshot", "--file", session.artifact_dir / "colour-neutral.png")
    edit("Timeline position", "2s")
    step("animate hue", "set-value", "--name", "Effect hue", "--role", "slider", "120")
    command("wait", "--ms", 1000)
    step("shifted colour", "screenshot", "--file", session.artifact_dir / "colour-shifted.png")
    saved_clips()
    data = project.read_bytes()
    root = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    effect = root.find(".//effect[@type='colour']")
    assert effect is not None
    keys = effect.findall("./property[@name='hue']/key")
    if not keys:
        keys = effect.findall("./property[@id='hue']/key")
    assert len(keys) == 2, ET.tostring(effect)
    print("Animated Colour effect authored and saved through UI", flush=True)
finally:
    session.stop_app()
