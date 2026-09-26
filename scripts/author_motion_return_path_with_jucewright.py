#!/usr/bin/env python3
"""Author the opening act of Return Path through Motion controls.

Run create_motion_return_path_assets.py into /private/tmp/return-path-assets
and provide the user soundtrack there as Soundtrack.wav. Later acts are not
authored by this opening-study script.
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
session.launch_app("return-path-authoring")
session.keep_app = keep
assets = Path("/private/tmp/return-path-assets")


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
    if name.endswith(".lua"):
        command("wait-for-locator", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact")
        step("set ribbon bake length", "fill", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact", "3.2")
        step("bake ribbon", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    elif name.endswith(".gif"):
        command("wait-for-locator", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
        step("trace animated raster", "click", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
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


def repeat_selected(start, duration):
    step("repeat selected motif", "press", "command + d", "--class", "MotionTimelineView")
    tab("Clip")
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
ET.SubElement(root, "composition", name="Return Path — opening study", duration="185.6", bpm="150", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "Return Path.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    step("size authoring workspace", "resize-window", "--w", 1600, "--h", 1000)
    import_source("Carrier.obj",0,51.2,{"red":1,"green":.55,"blue":.15,"scale.x":.14,"scale.y":.14,"scale.z":.14}, {
        "position.x":[(0,-.65),(6.4,-.5),(12.8,-.45),(19.2,-.45),(25.6,-.2),(38.4,.15),(51.1,.3)],
        "position.y":[(0,-.1),(6.4,.08),(12.8,0),(25.6,0),(38.4,.15),(51.1,.15)],
        "rotation.z":[(0,-25),(6.4,0),(12.8,-15),(25.6,-35),(38.4,-15),(51.1,0)]})
    import_source("Return path.txt",1.6,6.4,{"position.y":-.6,"scale.x":.45,"scale.y":.45,"scale.z":.45,"red":.8,"green":.8,"blue":.75})
    import_source("Anyone there.txt",12.8,4.8,{"position.y":-.5,"scale.x":.32,"scale.y":.32,"scale.z":.32,"red":1,"green":.55,"blue":.15})
    import_source("Receiver scan.gif",19.2,6.4,{"position.x":.55,"scale.x":.23,"scale.y":.23,"scale.z":.23,"red":.25,"green":.8,"blue":1})
    import_source("Receiver.obj",25.6,25.6,{"position.x":.55,"scale.x":.24,"scale.y":.24,"scale.z":.24,"red":.25,"green":.8,"blue":1})
    import_source("Relay packet.json",12.8,6.4,{"position.y":.05,"scale.x":.4,"scale.y":.08,"scale.z":.08,"red":1,"green":.55,"blue":.15})
    repeat_selected(25.6,25.6)
    import_source("Reply.lua",22.4,28.8,{"position.y":-.05,"scale.x":.5,"scale.y":.5,"scale.z":.5,"red":.25,"green":.8,"blue":1}, {
        "weight":[(22.4,0),(24,.3),(25.6,.3),(38.4,.65),(51.1,.65)]})
    import_source("Relay corridor.obj",32,19.2,{"position.y":0,"scale.x":.6,"scale.y":.6,"scale.z":.6,"red":.35,"green":.4,"blue":.45}, {
        "rotation.y":[(32,65),(38.4,45),(51.1,20)],
        "position.z":[(32,1.8),(38.4,1.2),(51.1,.3)],
        "weight":[(32,0),(35.2,.25),(44.8,.4),(51.1,.6)]})
    import_source("Soundtrack.wav",0,185.6,{})
    tab("Timeline")
    step("fit project", "press", "F", "--class", "MotionTimelineView")
    for moment in (3.2,14.4,20.8,27.2,36.8,48):
        seek(moment); command("wait", "--ms", 900)
        step("story frame " + str(moment),"screenshot","--file",session.artifact_dir/f"story-{moment:05.1f}.png")
    step("save opening", "press", "command + s", "--class", "MotionEditor")
    print("Opening narrative study saved; later acts not authored yet",flush=True)
finally:
    session.stop_app()
