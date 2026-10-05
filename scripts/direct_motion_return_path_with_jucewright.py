#!/usr/bin/env python3
"""Direct the Return Path narrative project through editor controls.

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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/Users/james/osci-render/artifacts/return-path/developed-study/Return Path developed.osci-motion"))
project = session.artifact_dir / "Return Path directed.osci-motion"
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



def add_effect(name,parameter,values):
    library=named(snapshot(),"Library tabs")
    target=find(library,lambda n:n.get("class")=="osci::TabBar::Tab" and n.get("name")=="Effects")
    step("open effects library","click",target["ref"])
    step("add narrative effect","click","--name",name,"--role","listItem","--click-count",2)
    command("wait-for-locator","--name","Effect "+parameter,"--role","slider","--exact")
    for index,(moment,value) in enumerate(values):
        seek(moment)
        step("set narrative effect","set-value","--name","Effect "+parameter,"--role","slider",value)
        if index==0:step("animate narrative effect","click","--name","Key effect "+parameter,"--exact")

try:
    command("wait-for-locator","--class","MotionEditor","--exact")
    subprocess.run(["open","-a",str(session.app_path),str(project)],check=True)
    step("size workspace","resize-window","--w",1600,"--h",1000)
    select_clip("Relay corridor.obj",34)
    add_effect("Swirl","swirl",[(32,0),(48,0),(64,.08),(73.6,.15),(76.7,0)])
    select_clip("Interference.lua",60)
    add_effect("Ripple","rippleDepth",[(51.2,0),(57.6,.08),(64,.2),(73.6,.3),(76.7,0)])
    camera("Contact",0,{"fov":24},{"position.x":[(0,-.15),(12.8,-.1),(25.6,0),(51.2,0)],"position.z":[(0,3.5),(19.2,3.6),(32,4.4),(51.2,4)]})
    camera("Pressure",51.2,{"fov":28},{"position.z":[(51.2,4),(64,3.5),(73.6,3.3),(76.7,4)],"rotation.z":[(51.2,0),(64,4),(73.6,-4),(76.7,0)]})
    camera("Listening",76.8,{"fov":24},{"position.x":[(76.8,0),(89.6,-.1),(102.4,0),(128,0)],"position.z":[(76.8,3.5),(89.6,3.1),(115.2,3.1),(128,4.4)]})
    camera("Return",128,{"fov":24},{"position.z":[(128,4.4),(153.6,3.5),(166.4,3),(179.2,2.7),(185.5,2.7)],"position.y":[(128,0),(166.4,-.12),(185.5,-.12)]})
    step("save directed story","press","command + s","--class","MotionEditor")
    tab("Timeline")
    for moment in (14.4,36.8,60.8,73.6,89.6,118.4,140.8,160,179.2):
        seek(moment);command("wait","--ms",900)
        step("directed story "+str(moment),"screenshot","--file",session.artifact_dir/f"directed-{moment:05.1f}.png")
    print("Camera/effect direction pass saved; full render pending",flush=True)
finally:
    session.stop_app()
