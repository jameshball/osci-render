#!/usr/bin/env python3
"""Improve Return Path beam exposure, relay hierarchy and camera framing through the UI.

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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/Users/james/osci-render/artifacts/return-path/directed-study/Return Path directed.osci-motion"))
project = session.artifact_dir / "Return Path polished.osci-motion"
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
    step("beam settings", "click", "--name", "settings", "--class", "osci::SvgButton", "--exact")
    intensity = find(snapshot(), lambda n: n.get("role") == "slider" and n.get("name") == "Line Intensity")
    step("clearer beam exposure", "set-value", intensity["ref"], 8)
    step("close beam settings", "press", "Escape")
    # The same relay forms progress from cold structure to a clear cyan bridge.
    for id_, time, colour in [(25, 40, (.55,.7,.85)), (35, 64, (.7,.45,.3)), (41, 140, (.35,.85,1))]:
        select_track(id_, time)
        seek(time)
        for channel, value in zip(("red", "green", "blue"), colour):
            edit(channel, value)
    # Keep the calm middle intimate; let the pressure passage occupy the frame.
    tab("Camera")
    for name, fov in [("Contact",20),("Pressure",20),("Listening",18),("Return",20)]:
        step("select camera " + name, "select-option", "--name", "Camera to edit", "--exact", "--text", name)
        edit("Camera fov", fov)
    step("save hierarchy pass", "press", "command + s", "--class", "MotionEditor")
    tab("Timeline")
    for moment in (14.4,36.8,54.4,67.2,89.6,118.4,137.6,156.8,180):
        seek(moment); command("wait", "--ms", 1000)
        step("hierarchy " + str(moment), "screenshot", "--file", session.artifact_dir / f"hierarchy-{moment:05.1f}.png")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    assert abs(float(saved.find("./beam/effects/parameter[@id='intensity']/parameter").get("value")) - 8) < .001
    for name, expected in [("Contact",20),("Pressure",20),("Listening",18),("Return",20)]:
        camera = saved.find("./composition/camera[@name='" + name + "']")
        assert camera is not None
        assert abs(float(camera.find("property[@name='fov']").get("base")) - expected) < .001
    print("Hierarchy pass saved for visual review", flush=True)
finally:
    session.stop_app()
