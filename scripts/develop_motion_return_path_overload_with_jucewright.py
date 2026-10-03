#!/usr/bin/env python3
"""Develop the overload into a continuous knot and release through the UI.

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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/Users/james/osci-render/artifacts/return-path/cue-study/Return Path cues.osci-motion"))
project = session.artifact_dir / "Return Path development.osci-motion"
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
    name = ET.fromstring(project.read_bytes()[8:].split(b"\0", 1)[0]).find("composition").get("name")
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", name, "--timeout-ms", 60000)
    step("size workspace", "resize-window", "--w", 1600, "--h", 1000)
    step("select interference source", "click", "--name", "Interference.lua", "--role", "listItem", "--exact", "--position", "6,12")
    step("edit interference", "click", "--name", "Edit Lua...", "--exact")
    code = (session.root_dir / "research/osci-motion/benchmark-sources/Interference.lua").read_text()
    step("author evolving knot", "fill", "--name", "Lua Code Editor", "--role", "editableText", "--exact", code)
    step("prepare whole phrase", "fill", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact", "25.6")
    step("bake evolving knot", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Undo Edit Lua source", "--role", "label", "--exact", "--timeout-ms", 120000)
    select_track(32, 60)
    for moment in (51.2,64,70.4,76.7):
        seek(moment); edit("rotation.z", 0)
    for moment,value in [(51.2,0),(57.6,.55),(64,1.1),(73.6,1.6),(76.7,0)]:
        seek(moment); edit("weight",value)
    # Retain the architecture as context, rather than several competing cages.
    select_track(25, 60)
    for moment,value in [(51.1,.6),(57.6,.35),(70.4,.2),(76.7,0)]:
        seek(moment); edit("weight",value)
    select_track(35, 60)
    for moment,value in [(57.6,0),(64,.12),(73.6,.18),(76.7,0)]:
        seek(moment); edit("weight",value)
    step("save developing overload", "press", "command + s", "--class", "MotionEditor")
    for moment in (54.4,60.8,67.2,73.6,75.8,80):
        seek(moment); command("wait", "--ms", 1000)
        step("development " + str(moment), "screenshot", "--file", session.artifact_dir / f"development-{moment:05.1f}.png")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    asset = saved.find("./composition/asset[@id='30']")
    assert abs(float(asset.find("bake").get("duration"))-25.6) < .000001
    assert len(saved.findall("./composition/track")) == 15
    assert len(saved.findall("./composition/marker")) == 9
    print("Continuous overload development saved; rendered review pending", flush=True)
finally:
    session.stop_app()
