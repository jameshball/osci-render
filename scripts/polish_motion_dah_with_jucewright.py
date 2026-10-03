#!/usr/bin/env python3
"""Refine an existing UI-authored DAH project through editor controls.

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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/Users/james/osci-render/artifacts/dah-motion-study/first-arrangement/DAH.osci-motion"))
project = session.artifact_dir / "DAH polished.osci-motion"
shutil.copyfile(source, project)
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("dah-polish")
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


def header(name):
    tab("Timeline")
    command("press", "F", "--class", "MotionTimelineView")
    for attempt in range(15):
        tree = snapshot()
        area = named(tree, "MotionTimelineView")["bounds"]
        target = find(tree, lambda n: n.get("componentName", "").startswith("Track name ") and n.get("value") == name)
        if target is not None and area["y"] + 26 <= target["bounds"]["y"] < area["y"] + area["h"] - 30:
            return target, area
        command("wheel", area["x"] + 250, area["y"] + 65, "--dy", "-0.2")
    raise RuntimeError("Cannot reveal track: " + name)


def select_clip(name, time):
    target, area = header(name)
    x = 170 + round(time * (area["w"] - 170 - 20) / 185.6)
    y = target["bounds"]["y"] - area["y"] + 8
    step("select " + name, "click", "--class", "MotionTimelineView", "--position", f"{x},{y}")
    tab("Properties")


def keys(property_, values):
    seek(values[0][0])
    edit(property_, values[0][1])
    step("key " + property_, "click", "--name", "Key " + property_, "--exact")
    for time, value in values[1:]:
        seek(time)
        edit(property_, value)


def menu(name):
    target, _ = header(name)
    id_ = target["componentName"].split()[-1]
    step("actions for " + name, "click", "--name", "Reorder track " + id_, "--exact")


def move_to(name, group):
    menu(name)
    step("choose grouping", "click", "--name", "Move to group", "--role", "menuItem", "--exact")
    step("move to " + group, "click", "--name", group, "--role", "menuItem", "--exact")


def rename_header(old, new):
    target, _ = header(old)
    edit(target["componentName"], new)


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    step("size workspace", "resize-window", "--w", 1600, "--h", 1000)
    # Separate the overlapping full-size outlines into a readable hierarchy.
    for name, starts, properties in [
        ("06 Mechanical wings.json", [9.6,48,76.8,134.4,166.4], {"position.x":.58,"position.y":.42,"scale.x":.32,"scale.y":.32,"scale.z":.32,"red":1,"green":.65,"blue":.2}),
        ("01 Helix ladder.obj", [6.4,44.8,64,115.2,153.6], {"position.x":-.3,"scale.x":.48,"scale.y":.48,"scale.z":.48}),
        ("Electric braid.lua", [44.8,64,96,140.8,166.4], {"position.x":.25,"position.y":.15,"scale.x":.48,"scale.y":.48,"scale.z":.48}),
        ("07 Travelling signal loom.json", [0,38.4,83.2,108.8,153.6], {"red":.3,"green":.7,"blue":1}),
    ]:
        for start in starts:
            select_clip(name,start+1)
            seek(start)
            for prop,value in properties.items():edit(prop,value)
        step("save refined motif", "press", "command + s", "--class", "MotionEditor")
    for moment in (16,54.4,99.2,156.8,180):
        seek(moment)
        command("wait", "--ms", 900)
        step("review refined " + str(moment), "screenshot", "--file", session.artifact_dir / f"polished-{moment:06.1f}.png")
    step("save refined composition", "press", "command + s", "--class", "MotionEditor")
    print("DAH composition hierarchy pass saved",flush=True)
finally:
    session.stop_app()
