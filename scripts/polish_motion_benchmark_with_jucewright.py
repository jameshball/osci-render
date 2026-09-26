#!/usr/bin/env python3
"""Refine an existing UI-authored Phase / Space project through editor controls.

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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/private/tmp/phase-space-arrangement-verified/Phase Space.osci-motion"))
project = session.artifact_dir / "Phase Space polished.osci-motion"
shutil.copyfile(source, project)
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("phase-space-polish")
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
    x = 170 + round(time * (area["w"] - 170 - 20) / 180)
    y = target["bounds"]["y"] - area["y"] + 8
    step("select " + name, "click", "--class", "MotionTimelineView", "--position", f"{x},{y}")
    tab("Object")


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
    command("wait-for-locator", "--name", "Hero diamond.obj", "--class", "juce::Label", "--exact")
    step("size workspace", "resize-window", "--w", 1600, "--h", 1000)
    for name, time, weight in [("Corridor.obj", 64, 3), ("Horizon.svg", 64, .12), ("Raster pulse.gif", 96, .15),
                               ("Left satellite.obj", 128, .22), ("Right satellite.obj", 128, .22), ("MIDI pulse.svg", 130, .25)]:
        select_clip(name, time)
        edit("weight", weight)
    select_clip("Hero diamond.obj", 170)
    # This curve is already animated; editing adds keys without toggling one off.
    seek(168)
    edit("rotation.y", 0)
    seek(176)
    edit("rotation.y", 88)
    keys("weight", [(176, 2), (179, 0)])
    select_clip("Title.txt", 174)
    keys("weight", [(178, 1), (179, 0)])
    tab("Camera")
    step("inspect corridor camera", "select-option", "--name", "Camera to edit", "--class", "juce::ComboBox", "--exact", "--text", "Corridor")
    for time, depth in [(48, 1.35), (79, 1.05)]:
        seek(time)
        edit("Camera position.z", depth)
    step("save balance and ending", "press", "command + s", "--class", "MotionEditor")
    menu("Hero diamond.obj")
    step("create main group", "click", "--name", "Group track", "--role", "menuItem", "--exact")
    rename_header("Group 1", "Main stage")
    menu("Main stage")
    step("create orbital group", "click", "--name", "Add nested group", "--role", "menuItem", "--exact")
    rename_header("Group 2", "Orbital rig")
    for name in ["Inner ring.obj", "Outer ring.obj", "Left satellite.obj", "Right satellite.obj", "Orbit crown.obj"]:
        move_to(name, "Orbital rig")
    for name in ["Title.txt", "Corridor.obj", "Horizon.svg", "Ribbons.lua", "Raster pulse.gif", "Accent.txt", "MIDI pulse.svg"]:
        move_to(name, "Main stage")
    group, _ = header("Orbital rig")
    group_id = group["componentName"].split()[-1]
    tree = snapshot()
    library = find(tree, lambda n: n.get("class") == "osci::TabBar" and n.get("name") == "Library tabs")
    effect_tab = find(library, lambda n: n.get("class") == "osci::TabBar::Tab" and n.get("name") == "Effects")
    step("open effects library", "click", effect_tab["ref"])
    tree = snapshot()
    effect = find(tree, lambda n: n.get("role") == "listItem" and n.get("name") == "Swirl")["bounds"]
    target = find(tree, lambda n: n.get("name") == "Reorder track " + group_id)["bounds"]
    step("drop swirl on orbital group", "drag-xy", effect["x"] + effect["w"] // 2, effect["y"] + effect["h"] // 2,
         target["x"] + target["w"] // 2, target["y"] + target["h"] // 2, "--steps", 20)
    for time, value in [(0, 0), (112, 0), (120, .08), (128, -.08), (136, .06), (144, 0)]:
        seek(time)
        step("shape orbital swirl", "set-value", "--name", "Effect swirl", "--role", "slider", value)
        step("key orbital effect", "click", "--name", "Key effect swirl", "--exact")
    step("save polished arrangement", "press", "command + s", "--class", "MotionEditor")
    for moment in (64, 96, 128, 176, 178, 179):
        seek(moment)
        command("wait", "--ms", 700)
        step("polished frame " + str(moment), "screenshot", "--file", session.artifact_dir / f"polished-{moment:03}.png")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    assert len(saved.findall("./composition/track")) == 14
    assert len(saved.findall("./composition/group")) == 2
    assert saved.findall("./composition/group/effect"), "Group effect missing"
    swirl = saved.find("./composition/group/effect/property[@name='swirl']")
    assert [float(key.get("time")) for key in swirl.findall("key")] == [0, 112, 120, 128, 136, 144]
    for name, end_time in [("Hero diamond.obj", 67), ("Title.txt", 11)]:
        track = next(t for t in saved.findall("./composition/track") if t.get("name") == name)
        ending = track.findall("clip")[-1].find("property[@name='weight']/key[@value='0.0']")
        assert ending is not None and float(ending.get("time")) == end_time, "Ending fade missing: " + name
    assert float(saved.find("composition").get("duration")) == 180
    print("Polish authored and saved; inspect beam balance, keyed group effect and dark ending", flush=True)
finally:
    session.stop_app()
