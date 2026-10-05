#!/usr/bin/env python3
"""Author a directed answer and clarify the overload framing through the UI.

Set MOTION_BENCHMARK_PROJECT to the authoring script's saved project if needed.
A copy is edited in the artifact directory; source project bytes are untouched.
"""
import json
import os
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/Users/james/osci-render/artifacts/return-path/development-study/Return Path development.osci-motion"))
project = session.artifact_dir / "Return Path answer.osci-motion"
shutil.copyfile(source, project)
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("return-path-direction")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def find(value, predicate):
    if isinstance(value, dict):
        if predicate(value):
            return value
        for child in value.values():
            result = find(child, predicate)
            if result is not None:
                return result
    elif isinstance(value, list):
        for child in value:
            result = find(child, predicate)
            if result is not None:
                return result
    return None


def snapshot():
    return json.loads(command("snapshot", "--json", "--full"))


def named(tree, name):
    return find(tree, lambda n: n.get("componentName") == name or n.get("class") == name)


def edit(name, value):
    target = named(snapshot(), name)
    assert target is not None, name
    step("open " + name, "click", target["ref"], "--click-count", 2)
    field = named(snapshot(), name)
    editor = named(field, "juce::TextEditor")
    assert editor is not None, name
    step("set " + name, "fill", editor["ref"], str(value))
    step("commit " + name, "press", "Return")


def tab(name):
    step("inspect " + name, "click", "--name", name, "--class", "osci::TabBar::Tab", "--exact")


def seek(time):
    edit("Timeline position", str(time) + "s")
    # Wait for the audio transport and the editor timer to acknowledge the seek.
    command("wait-for-value", "--component-name", "Timeline position", "--value", f"{time:.3f}s", "--timeout-ms", 5000)


def select_track(id_, time):
    tab("Timeline")
    command("press", "F", "--class", "MotionTimelineView")
    for attempt in range(24):
        tree = snapshot()
        area = named(tree, "MotionTimelineView")["bounds"]
        target = named(tree, "Track name " + str(id_))
        if target is not None and area["y"] + 48 <= target["bounds"]["y"] < area["y"] + area["h"] - 30:
            x = 170 + round(time * (area["w"] - 190) / 185.6)
            y = target["bounds"]["y"] - area["y"] + 8
            step("select track " + str(id_), "click", "--class", "MotionTimelineView", "--position", f"{x},{y}")
            tab("Properties")
            return
        direction = .3 if target is not None and target["bounds"]["y"] < area["y"] + 48 else -.3
        command("wheel", area["x"] + 250, area["y"] + 65, "--dy", direction)
    raise RuntimeError("Cannot reveal track " + str(id_))

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    data = project.read_bytes()
    name = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition").get("name")
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", name, "--timeout-ms", 60000)
    step("size workspace", "resize-window", "--w", 1600, "--h", 1000)
    tab("Camera")
    step("choose pressure lens", "select-option", "--name", "Camera to edit", "--exact", "--text", "Pressure")
    seek(67.2); edit("Camera fov",24); edit("Camera position.x",.1)
    # A single answer travels from cyan to amber, then expands on contact.
    seek(102.4)
    source = session.root_dir / "research/osci-motion/benchmark-sources/Answer pulse.svg"
    step("import answer pulse", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Answer pulse.svg", "--class", "juce::Label", "--exact")
    tab("Properties"); edit("Clip duration",25.6)
    tab("Properties")
    for prop,value in {"red":.2,"green":.8,"blue":1,"position.y":0,"position.z":0}.items():
        edit(prop,value)
    animation = {
        "position.x":[(102.4,.65),(108.8,.55),(112,.2),(115.2,-.15),(116.8,-.16),(121.6,-.25)],
        "weight":[(102.4,0),(108.4,0),(108.8,.08),(115.2,.16),(116.8,.08),(121.6,0)],
    }
    for axis in ("x","y","z"):
        animation["scale."+axis] = [(102.4,.025),(108.8,.025),(115.2,.025),(116.8,.13),(121.6,.18)]
    for prop,values in animation.items():
        seek(values[0][0]); edit(prop,values[0][1])
        step("animate " + prop, "click", "--name", "Key " + prop, "--exact")
        for moment,value in values[1:]:
            seek(moment); edit(prop,value)
    select_track(3,115.2)
    for moment,value in [(108.8,0),(112,-25),(115.2,-55),(116.8,-55),(121.6,-35)]:
        seek(moment); edit("rotation.z",value)
    step("save directed answer", "press", "command + s", "--class", "MotionEditor")
    for moment in (60.8,67.2,73.6,109.6,112,115.2,116.8,120):
        seek(moment); command("wait", "--ms", 1000)
        step("answer " + str(moment), "screenshot", "--file", session.artifact_dir / f"answer-{moment:05.1f}.png")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    assert len(saved.findall("./composition/track")) == 16
    pulse = saved.find("./composition/track/clip[@name='Answer pulse.svg']")
    assert abs(float(pulse.get("start"))-102.4) < .000001
    assert abs(float(pulse.get("duration"))-25.6) < .000001
    print("Directed answer and pressure framing saved; rendered review pending", flush=True)
finally:
    session.stop_app()
