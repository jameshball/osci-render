#!/usr/bin/env python3
"""Author the Phase / Space arrangement study through the actual Motion interface.

The initial project is empty. Every source, clip, property and key is created
through user-facing controls. This is an arrangement study requiring visual and export review.
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
    step("inspect " + name, "click", "--name", name, "--class", "MotionTabs::Tab", "--exact")


def seek(seconds):
    edit("Timeline position", str(seconds) + "s")


def import_source(name, start, duration, properties, animation=None):
    seek(start)
    step("import " + name, "drop-files", "--file", assets / name, "--class", "MotionEditor", "--exact")
    if name.endswith(".lua"):
        command("wait-for-locator", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact")
        step("set ribbon bake length", "fill", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact", "4")
        step("bake ribbon", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    elif name.endswith(".gif"):
        command("wait-for-locator", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
        step("trace animated raster", "click", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", name, "--class", "juce::Label", "--exact")
    tab("Properties")
    edit("Clip duration", duration)
    if properties or animation:
        tab("Properties")
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


def repeat_selected(start, duration):
    step("repeat selected motif", "press", "command + d", "--class", "MotionTimelineView")
    tab("Properties")
    edit("Clip start", start)
    edit("Clip duration", duration)


def camera(name, start, properties, animation=None):
    seek(start)
    tab("Camera")
    step("add output camera", "click", "--name", "Add", "--class", "juce::TextButton", "--exact")
    edit("Camera name", name)
    for key, value in properties.items():
        edit("Camera " + key, value)
    for key, keys in (animation or {}).items():
        seek(start)
        edit("Camera " + key, keys[0][1])
        step("key camera " + key, "click", "--name", "Key camera " + key, "--exact")
        for moment, value in keys[1:]:
            seek(moment)
            edit("Camera " + key, value)
    seek(start)
    step("cut to " + name, "click", "--name", "Cut here", "--exact")


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Phase / Space — arrangement study", duration="180", bpm="120", fps="30")
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
    repeat_selected(112, 68)
    import_source("Inner ring.obj", 4, 44, {"red": .15, "green": .85, "blue": 1, "scale.x": .72, "scale.y": .72, "scale.z": .72}, {
        "rotation.y": [(4, 88), (16, 30), (32, -30), (47, 30)],
        "rotation.x": [(4, 0), (16, 25), (32, -15), (47, 25)],
    })
    repeat_selected(116, 52)
    import_source("Outer ring.obj", 8, 40, {"red": .65, "green": .35, "blue": 1}, {
        "rotation.x": [(8, 88), (16, -20), (32, 35), (47, -20)],
        "rotation.y": [(8, 0), (16, -15), (32, 15), (47, -15)],
    })
    repeat_selected(120, 48)
    import_source("Title.txt", 0, 12, {"position.y": -.8, "scale.x": .65, "scale.y": .65, "scale.z": .65, "red": .55, "green": .8, "blue": 1})
    repeat_selected(168, 12)
    import_source("Corridor.obj", 48, 32, {"red": .15, "green": .8, "blue": 1}, {
        "rotation.z": [(48, 0), (64, 45), (79, 90)],
    })
    import_source("Left satellite.obj", 16, 32, {"position.x": -.65, "scale.x": .12, "scale.y": .12, "scale.z": .12, "red": 1, "green": .55, "blue": .15}, {
        "position.y": [(16, -.35), (24, .35), (32, -.35), (40, .35), (47, 0)],
    })
    repeat_selected(112, 48)
    import_source("Right satellite.obj", 24, 24, {"position.x": .65, "scale.x": .12, "scale.y": .12, "scale.z": .12, "red": .5, "green": .35, "blue": 1}, {
        "position.y": [(24, -.35), (32, .35), (40, -.35), (47, 0)],
    })
    repeat_selected(120, 40)
    import_source("Orbit crown.obj", 112, 32, {"scale.x": .85, "scale.y": .85, "scale.z": .85, "red": .7, "green": .4, "blue": 1}, {
        "rotation.x": [(112, 80), (128, 45), (143, 80)],
    })
    import_source("Horizon.svg", 48, 32, {"position.y": -.75, "scale.x": .8, "scale.y": .12, "scale.z": .12, "red": 1, "green": .6, "blue": .2})
    import_source("Ribbons.lua", 80, 32, {"scale.x": .8, "scale.y": .8, "scale.z": .8}, {
        "rotation.y": [(80, -20), (96, 20), (111, -20)],
    })
    import_source("Raster pulse.gif", 88, 16, {"position.x": .6, "position.y": .45, "scale.x": .15, "scale.y": .15, "scale.z": .15})
    import_source("Accent.txt", 48, 8, {"position.y": -.8, "scale.x": .5, "scale.y": .5, "scale.z": .5, "red": .8, "green": .7, "blue": 1})
    import_source("MIDI pulse.svg", 128, 8, {"position.y": -.7, "scale.x": .12, "scale.y": .12, "scale.z": .12, "red": 1, "green": .5, "blue": .15})
    step("import pulse score", "drop-files", "--file", assets / "Pulse phrase.mid", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Assign to selected clip", "--exact")
    step("assign pulse score", "click", "--name", "Assign to selected clip", "--exact")
    camera("Main", 0, {})
    camera("Corridor", 48, {"fov": 65}, {"position.z": [(48, 2), (79, 1.4)]})
    camera("Drift", 80, {"position.x": -.4, "rotation.y": -6})
    seek(112)
    step("return to main camera", "select-option", "--name", "Camera to edit", "--class", "juce::ComboBox", "--exact", "--text", "Main")
    step("cut back to main", "click", "--name", "Cut here", "--exact")
    import_source("Phase Space.wav", 0, 180, {})
    tab("Timeline")
    step("fit composition", "press", "F", "--class", "MotionTimelineView")
    for moment in (0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 168, 179):
        seek(moment)
        command("wait", "--ms", 700)
        step("opening frame " + str(moment), "screenshot", "--file", session.artifact_dir / f"opening-{moment:03}.png")
    step("save opening study", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    tracks = saved.findall("./composition/track")
    assert len(tracks) == 14
    assert len(saved.findall("./composition/camera")) == 3
    assert len(saved.findall("./composition/cameraCut")) == 4
    assert saved.findall(".//midi/note"), "Imported MIDI was not assigned"
    assert len(saved.findall(".//key")) >= 20, "Animation was not authored"
    print("Fourteen-layer arrangement saved; pacing, grouping, effects and export review remain", flush=True)
finally:
    session.stop_app()
