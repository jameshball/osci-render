#!/usr/bin/env python3
"""Make bridge traffic activate a route and stage a shared arrival.

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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", str(session.root_dir / "artifacts/return-path/evolving-study/Return Path evolving.osci-motion")))
project = session.artifact_dir / "Return Path activated.osci-motion"
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
    data = project.read_bytes()
    original = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    name = original.find("composition").get("name")
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", name, "--timeout-ms", 60000)
    step("size workspace", "resize-window", "--w", 1600, "--h", 1000)
    # Retain the prior bridge/packet layers muted for comparison.
    for track in (41,44):
        select_track(track,140)
        step("mute superseded bridge layer", "click", "--name", "Mute track " + str(track), "--exact")
    select_track(38,140)
    for moment,value in [(124.8,.4),(128,0),(153.6,.06),(166.4,.06),(172.8,.12)]:
        seek(moment); edit("weight",value)
    seek(128)
    source = session.root_dir / "research/osci-motion/benchmark-sources/Bridge activation.mp4"
    step("import bridge activation", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    step("bridge tracing detail", "select-option", "--name", "Image detail", "--class", "juce::ComboBox", "--exact", "--text", "256 px")
    step("bridge beam samples", "select-option", "--name", "Image samples per frame", "--class", "juce::ComboBox", "--exact", "--text", "2048")
    step("bake bridge video", "click", "--name", "Prepare video", "--exact")
    command("wait-for-locator", "--name", "Bridge activation.mp4", "--class", "juce::Label", "--exact", "--timeout-ms", 120000)
    tab("Properties")
    for prop,value in {"scale.x":.6,"scale.y":.6,"scale.z":.6,"weight":.8}.items():
        edit(prop,value)
    select_track(3,140)
    for moment,value in [(128,0),(140.8,0),(153.6,0),(160,.04),(166.4,0),(179.2,0)]:
        seek(moment); edit("position.y",value)
    for moment,value in [(128,.3),(129.6,.5),(153.6,.7),(166.4,.5),(179.2,.3)]:
        seek(moment); edit("weight",value)
    select_track(15,154)
    seek(166.4); edit("position.x",.15)
    for prop,values in {
        "position.y":[(102.4,0),(153.6,0),(160,.04),(166.4,0)],
        "scale.x":[(102.4,.24),(150.4,.24),(153.6,.17),(155.2,.19),(166.4,.19)],
        "scale.y":[(102.4,.24),(150.4,.24),(153.6,.2),(155.2,.21),(166.4,.21)],
    }.items():
        seek(values[0][0]); edit(prop,values[0][1])
        step("animate " + prop, "click", "--name", "Key " + prop, "--exact")
        for moment,value in values[1:]:
            seek(moment); edit(prop,value)
    tab("Camera")
    step("choose return lens", "select-option", "--name", "Camera to edit", "--exact", "--text", "Return")
    for moment,value in [(128,18),(140.8,16),(153.6,15),(160,15),(166.4,18),(179.2,20)]:
        seek(moment); edit("Camera fov",value)
    seek(128); edit("Camera position.x",.04)
    step("follow shared arrival", "click", "--name", "Key camera position.x", "--exact")
    for moment,value in [(140.8,.04),(153.6,.13),(166.4,.15),(179.2,.15)]:
        seek(moment); edit("Camera position.x",value)
    step("save activated revision", "press", "command + s", "--class", "MotionEditor")
    for moment in (130.4,133.6,137.6,140,141.2,148,153.6,160,166.4,180):
        seek(moment); command("wait", "--ms", 800)
        step("activation " + str(moment), "screenshot", "--file", session.artifact_dir / f"activation-{moment:05.1f}.png")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    video = saved.find("composition/track/clip[@name='Bridge activation.mp4']")
    assert abs(float(video.get("start"))-128)<1e-9 and abs(float(video.get("duration"))-38.4)<1e-9
    assert all(saved.find("composition/track[@id='"+str(id_)+"']").get("muted")=="1" for id_ in (41,44))
    assert len(saved.findall("composition/track")) == 17
    assert abs(float(video.find("property[@name='weight']").get("base")) - .8) < 1e-9
    reply = saved.find("composition/track[@id='38']/clip")
    reply_keys = reply.findall("property[@name='weight']/key")
    for moment in (153.6, 166.4):
        matching = [key for key in reply_keys if abs(float(key.get("time")) + float(reply.get("start")) - moment) < 1e-9]
        assert len(matching) == 1 and abs(float(matching[0].get("value")) - .06) < 1e-9
    print("Activated bridge and shared arrival saved; export review pending", flush=True)
finally:
    session.stop_app()
