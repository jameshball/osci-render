#!/usr/bin/env python3
"""Develop the saved Return Path opening into a complete narrative study.

Works on a copy through actual editor controls; renders still require review.
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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/Users/james/osci-render/artifacts/return-path/opening-study/Return Path.osci-motion"))
project = session.artifact_dir / "Return Path developed.osci-motion"
shutil.copyfile(source, project)
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("return-path-development")
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



assets = Path("/private/tmp/return-path-assets")
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



def extend(name,at,end):
    select_clip(name,at)
    tab("Properties")
    edit("Clip duration",end)
    tab("Properties")

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    step("size workspace", "resize-window", "--w", 1600, "--h", 1000)
    for at in (14,27):
        select_clip("Relay packet.json",at);edit("weight",.045)
    extend("Carrier.obj",1,185.6)
    keys("weight",[(0,.3),(76.8,.3),(128,.3),(179.2,.3),(184,0)])
    keys("position.x",[(51.2,.3),(57.6,.35),(64,.15),(70.4,.35),(76.8,.1),(89.6,-.25),(102.4,-.25),(115.2,-.15),(128,-.1),(153.6,.35),(166.4,.15),(179.2,.15)])
    keys("position.y",[(51.2,.15),(64,-.1),(76.8,0),(102.4,0),(128,0),(153.6,.12),(166.4,.08),(179.2,0)])
    keys("rotation.z",[(51.2,0),(57.6,25),(64,-30),(76.8,0),(102.4,0),(128,-25),(153.6,0),(179.2,0)])
    extend("Receiver.obj",27,51.2)
    repeat_selected(102.4,83.2)
    tab("Properties")
    keys("position.x",[(102.4,.65),(115.2,.45),(128,.4),(153.6,.35),(179.2,.15)])
    keys("weight",[(102.4,.1),(115.2,.4),(153.6,.4),(179.2,.4),(184,0)])
    extend("Relay corridor.obj",34,44.8)
    keys("weight",[(32,.15),(44.8,.6),(57.6,.9),(70.4,1),(76.7,0)])
    keys("rotation.y",[(51.2,20),(64,-15),(70.4,10),(76.7,80)])
    select_clip("Relay packet.json",27);tab("Properties");edit("Clip duration",51.2);tab("Properties")
    select_clip("Reply.lua",24);tab("Properties");edit("Clip duration",54.4);tab("Properties")
    keys("weight",[(51.2,.65),(64,.25),(76.7,0)])
    import_source("Interference.lua",51.2,25.6,{"red":1,"green":.2,"blue":.12,"scale.x":.7,"scale.y":.7,"scale.z":.7}, {
        "weight":[(51.2,0),(57.6,.35),(64,.7),(73.6,1),(76.7,0)],
        "rotation.z":[(51.2,0),(64,25),(70.4,-25),(76.7,75)]})
    import_source("Relay corridor.obj",57.6,19.2,{"red":.35,"green":.4,"blue":.45,"scale.x":.75,"scale.y":.75,"scale.z":.75,"rotation.z":90}, {
        "rotation.x":[(57.6,70),(64,35),(73.6,0),(76.7,80)],
        "weight":[(57.6,0),(64,.3),(73.6,.7),(76.7,0)]})
    import_source("Reply.lua",89.6,96,{"red":.25,"green":.8,"blue":1,"scale.x":.3,"scale.y":.3,"scale.z":.3,"position.y":-.1}, {
        "weight":[(89.6,0),(96,.12),(102.4,.2),(115.2,.4),(128,.4),(179.2,.2),(184,0)],
        "scale.x":[(89.6,.1),(102.4,.25),(128,.45),(153.6,.25),(179.2,.15)]})
    import_source("Relay corridor.obj",128,51.2,{"red":.3,"green":.6,"blue":.7,"scale.x":.7,"scale.y":.7,"scale.z":.7}, {
        "rotation.y":[(128,75),(140.8,45),(153.6,0),(166.4,0),(179.1,0)],
        "position.z":[(128,1.8),(140.8,1),(153.6,0),(179.1,1.8)],
        "weight":[(128,0),(140.8,.5),(153.6,.6),(166.4,.35),(179.1,0)]})
    import_source("Relay packet.json",128,38.4,{"red":.25,"green":.8,"blue":1,"scale.x":.5,"scale.y":.08,"scale.z":.08,"weight":.045})
    import_source("Still here.txt",176,8,{"red":.7,"green":.8,"blue":.8,"position.y":-.5,"scale.x":.38,"scale.y":.38,"scale.z":.38}, {"weight":[(176,0),(177.6,.8),(182.4,.8),(184,0)]})
    tab("Timeline");step("fit story", "press", "F", "--class", "MotionTimelineView")
    for moment in (14.4,36.8,60.8,73.6,83.2,99.2,118.4,140.8,160,179.2,184.8):
        seek(moment);command("wait","--ms",900)
        step("developed story "+str(moment),"screenshot","--file",session.artifact_dir/f"developed-{moment:05.1f}.png")
    step("save story", "press", "command + s", "--class", "MotionEditor")
    print("Full Return Path story study saved; render and pacing review pending",flush=True)
finally:
    session.stop_app()
