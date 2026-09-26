#!/usr/bin/env python3
"""Author the opening Phase / Space study through the actual Motion interface.

The initial project is empty. Every source, clip, property and key is created
through user-facing controls. This is an opening study, not the completed film.
Run create_motion_benchmark_assets.py and create_motion_benchmark_soundtrack.py
with /private/tmp/phase-space-assets as their destination first.
"""
import json
import struct
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("phase-space-authoring")
session.keep_app = keep
assets = Path("/private/tmp/phase-space-assets")


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


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


def tab(name):
    step("inspect " + name, "click", "--name", name, "--class", "osci::TabBar::Tab", "--exact")


def seek(seconds):
    edit("Timeline position", str(seconds) + "s")


def import_source(name, start, duration, properties, animation=None):
    seek(start)
    step("import " + name, "drop-files", "--file", assets / name, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", name, "--class", "juce::Label", "--exact")
    tab("Clip")
    edit("Clip duration", duration)
    if properties or animation:
        tab("Object")
        for property_, value in properties.items():
            edit(property_, value)
        for property_, keys in (animation or {}).items():
            seek(start)
            edit(property_, keys[0][1])
            step("key " + property_, "click", "--name", "Key " + property_, "--exact")
            for time, value in keys[1:]:
                seek(time)
                edit(property_, value)
    step("save imported layer", "press", "command + s", "--class", "MotionEditor")


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Phase / Space — opening study", duration="180", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "Phase Space.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    step("size authoring workspace", "resize-window", "--w", 1600, "--h", 1000)
    import_source("Hero diamond.obj", 0, 48, {"red": 1, "green": .5, "blue": .12, "scale.x": .65, "scale.y": .65, "scale.z": .65}, {
        "rotation.y": [(0, 88), (8, 15), (16, 0), (32, -25), (47, 10)],
        "rotation.z": [(0, 0), (16, 0), (32, 25), (47, 0)],
    })
    import_source("Inner ring.obj", 4, 44, {"red": .15, "green": .85, "blue": 1, "scale.x": .72, "scale.y": .72, "scale.z": .72}, {
        "rotation.y": [(4, 88), (16, 30), (32, -30), (47, 30)],
        "rotation.x": [(4, 0), (16, 25), (32, -15), (47, 25)],
    })
    import_source("Outer ring.obj", 8, 40, {"red": .65, "green": .35, "blue": 1}, {
        "rotation.x": [(8, 88), (16, -20), (32, 35), (47, -20)],
        "rotation.y": [(8, 0), (16, -15), (32, 15), (47, -15)],
    })
    import_source("Title.txt", 0, 12, {"position.y": -.8, "scale.x": .65, "scale.y": .65, "scale.z": .65, "red": .55, "green": .8, "blue": 1})
    import_source("Phase Space.wav", 0, 180, {})
    step("fit composition", "press", "F", "--class", "MotionTimelineView")
    for moment in (0, 8, 16, 32, 47):
        seek(moment)
        command("wait", "--ms", 700)
        step("opening frame " + str(moment), "screenshot", "--file", session.artifact_dir / f"opening-{moment:03}.png")
    step("save opening study", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    tracks = saved.findall("./composition/track")
    assert len(tracks) == 5
    assert len(saved.findall(".//key")) >= 20, "Animation was not authored"
    print("Opening study saved; visual critique and full arrangement remain", flush=True)
finally:
    session.stop_app()
