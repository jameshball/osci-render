#!/usr/bin/env python3
"""Align Return Path rebuilding and closing compositions through the UI.

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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/Users/james/osci-render/artifacts/return-path/hierarchy-study/Return Path polished.osci-motion"))
project = session.artifact_dir / "Return Path bridge.osci-motion"
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
    step("inspect " + name, "click", "--name", name, "--class", "MotionTabs::Tab", "--exact")


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
        if target is not None and area["y"] + 26 <= target["bounds"]["y"] < area["y"] + area["h"] - 30:
            x = 170 + round(time * (area["w"] - 190) / 185.6)
            y = target["bounds"]["y"] - area["y"] + 8
            step("select track " + str(id_), "click", "--class", "MotionTimelineView", "--position", f"{x},{y}")
            tab("Properties")
            return
        direction = .3 if target is not None and target["bounds"]["y"] < area["y"] + 26 else -.3
        command("wheel", area["x"] + 250, area["y"] + 65, "--dy", direction)
    raise RuntimeError("Cannot reveal track " + str(id_))

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    step("size workspace", "resize-window", "--w", 1600, "--h", 1000)
    select_track(41, 140)
    # The rebuilt route stays aligned with the carrier's direction of travel.
    # Existing keys are overwritten; editing an animated field updates its key.
    for moment in (128,140.8,153.6,166.4,179.1):
        seek(moment); edit("rotation.y", 90)
    for moment in (128,140.8,153.6,179.1):
        seek(moment); edit("position.z", 0)
    edit("scale.x", .35); edit("scale.y", .22); edit("scale.z", .55)
    edit("position.y", .04)
    select_track(3, 140)
    for moment,value in [(128,-.4),(140.8,-.1),(153.6,.35)]:
        seek(moment); edit("position.x", value)
    select_track(47, 180)
    seek(180)
    edit("position.x", .15); edit("position.y", -.27)
    for axis in ("x","y","z"):
        edit("scale."+axis, .28)
    # Give the title enough trace allocation to retain its letter corners.
    for moment,value in [(177.6,1.5),(182.4,1.5)]:
        seek(moment); edit("weight",value)
    for channel,value in [("red",.5),("green",.6),("blue",.6)]:
        edit(channel,value)
    tab("Camera")
    step("select return lens", "select-option", "--name", "Camera to edit", "--exact", "--text", "Return")
    seek(128); edit("Camera fov",20)
    step("animate return lens", "click", "--name", "Key camera fov", "--exact")
    for moment,value in [(140.8,16),(153.6,18),(166.4,20),(185.5,20)]:
        seek(moment); edit("Camera fov",value)
    step("save bridge direction", "press", "command + s", "--class", "MotionEditor")
    for moment in (128.8,137.6,148.8,156.8,180):
        seek(moment); command("wait", "--ms", 1000)
        step("bridge " + str(moment), "screenshot", "--file", session.artifact_dir / f"bridge-{moment:05.1f}.png")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    bridge = saved.find("./composition/track[@id='41']/clip")
    rotation_keys = bridge.findall("property[@name='rotation.y']/key")
    assert len(rotation_keys) == 5
    assert all(abs(float(key.get("value")) - 90) < .001 for key in rotation_keys)
    assert all(abs(float(key.get("value"))) < .001 for key in bridge.findall("property[@name='position.z']/key"))
    title = saved.find("./composition/track[@id='47']/clip")
    assert abs(float(title.find("property[@name='position.y']").get("base")) + .27) < .001
    assert len(saved.findall("./composition/track")) == 15
    print("Aligned bridge and unified ending saved", flush=True)
finally:
    session.stop_app()
