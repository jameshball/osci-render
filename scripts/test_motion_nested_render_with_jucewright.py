#!/usr/bin/env python3
"""Load, render and reopen two shared composition instances in the real app.

The fixture is authored through the file format because nested editing commands
are not exposed yet. This checks rendering/persistence, not nested editing UX.
"""
import copy
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
    subprocess.run(["open", "-a", str(session.app_path), str(seed)], check=True)
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    source = session.artifact_dir / "Motif.txt"
    source.write_text("RETURN")
    step("import motif", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Motif.txt", "--class", "juce::Label", "--exact")
    step("save source", "press", "command + s", "--class", "MotionEditor")
    root = read_project(seed)
    composition = root.find("composition")
    track = composition.find("track")
    composition.remove(track)
    definition = ET.SubElement(composition, "definition", id="1000")
    content = ET.SubElement(definition, "composition", name="Shared motif", duration="5", bpm="120", fps="30")
    content.append(track)
    prototype = track.find("clip")
    for index, x in enumerate((-0.45, 0.45)):
        instance_track = ET.SubElement(composition, "track", id=str(1010 + index), name=f"Instance {index + 1}", kind="visual")
        instance = copy.deepcopy(prototype)
        instance.attrib.pop("asset")
        instance.set("id", str(1020 + index))
        instance.set("composition", "1000")
        instance.set("name", f"Shared motif {index + 1}")
        instance.set("rate", str(index + 1))
        for prop in instance.findall("property"):
            name = prop.get("name")
            if name in ("red", "green", "blue"):
                prop.set("base", "1")
            elif name.startswith("scale."):
                prop.set("base", "0.45")
            elif name == "position.x":
                prop.set("base", str(x))
            elif name == "position.y":
                prop.set("base", str(-0.25 if index == 0 else 0.25))
        instance_track.append(instance)
    nested = session.artifact_dir / "nested.osci-motion"
    write_project(root, nested)
    subprocess.run(["open", "-a", str(session.app_path), str(nested)], check=True)
    command("wait", "--ms", 1000)
    snapshot = command("snapshot", "--json", "--full")
    assert "Reusable composition rendering is not connected" not in snapshot
    step("nested output", "screenshot", "--file", session.artifact_dir / "nested-output.png")
    step("save nested", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(nested)
    assert len(saved.findall("./composition/definition")) == 1
    assert len(saved.findall("./composition/track/clip[@composition='1000']")) == 2
    session.keep_app = False
    session.launch_app("nested-reopen")
    session.keep_app = keep
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(nested)], check=True)
    command("wait", "--ms", 1000)
    step("reopened nested output", "screenshot", "--file", session.artifact_dir / "nested-reopened.png")
    print("Nested rendering fixture saved and reopened; inspect both output screenshots", flush=True)
finally:
    session.stop_app()
