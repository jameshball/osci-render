#!/usr/bin/env python3
"""Verify authored beam settings and scope timing, and inspect the benchmark with upsampling enabled.

Requires the UI-authored Phase / Space benchmark. Override its location with
MOTION_BENCHMARK_PROJECT. Only a copy in the automation artifact directory changes.
"""
import json
import os
import shutil
import struct
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/private/tmp/phase-space-polished-reviewed/Phase Space polished.osci-motion"))
project = session.artifact_dir / "Phase Space beam.osci-motion"


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


if source.exists():
    shutil.copyfile(source, project)
else:
    # Without the benchmark, a three-minute study with one diamond stands in.
    root = ET.Element("motion-project", schema="1")
    composition = ET.SubElement(root, "composition", name="Phase Space", duration="180", bpm="120", fps="30")
    asset = ET.SubElement(composition, "asset", id="1", name="Hero diamond.obj", extension=".obj")
    asset.text = encoded(b"v 0 1 0\nv 1 0 0\nv 0 -1 0\nv -1 0 0\nl 1 2 3 4 1\n")
    track = ET.SubElement(composition, "track", id="2", name="Hero diamond.obj", kind="visual")
    clip = ET.SubElement(track, "clip", id="3", asset="1", name="Hero diamond.obj", start="0", duration="180", offset="0", rate="1", timeBase="seconds", contentBpm="120")
    for group, base in (("position", 0), ("rotation", 0), ("scale", 1)):
        for axis in "xyz":
            ET.SubElement(clip, "property", name=f"{group}.{axis}", base=str(base))
    for prop, base in (("red", 1), ("green", 1), ("blue", 1), ("weight", 1)):
        ET.SubElement(clip, "property", name=prop, base=str(base))
    ET.SubElement(composition, "camera", id="9", name="Camera")
    xml = ET.tostring(root, encoding="utf-8")
    project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("beam-settings")
session.keep_app = keep


def command(*args):
    return subprocess.check_output(session.cli(*args), text=True)


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def snapshot():
    return json.loads(command("snapshot", "--json", "--full"))


def intensity():
    return next(node for node in nodes(snapshot()) if node.get("componentName") == "intensity" and node.get("class") == "motion::ui::ScrubField")


def scope_field(label):
    return next(node for node in nodes(snapshot()) if node.get("class") == "motion::ui::ScrubField" and "Scope " + label in (node.get("name"), node.get("componentName")))


def saved():
    data = project.read_bytes()
    assert struct.unpack_from("<I", data)[0] == 0x21324356
    length = struct.unpack_from("<I", data, 4)[0]
    return ET.fromstring(data[8:8 + length])


def saved_scope():
    composition = saved().find("composition")
    return [float(composition.get(name)) for name in ("scopeDwell", "scopeTravel", "scopeSettle")]


def saved_intensity():
    prop = saved().find("composition/scopeBeam/property[@name='intensity']")
    return float(prop.get("base")), [(round(float(key.get("time")), 3), round(float(key.get("value")), 3)) for key in prop.findall("key")]


def open_project():
    session.open_project(project)
    command("wait-for-locator", "--name", "Hero diamond.obj", "--class", "juce::Label", "--exact")


def open_scope():
    # The cog selects the Scope: its beam and display properties are rows in Properties.
    step("select the Scope", "click", "--name", "Scope settings", "--exact")
    command("wait-for-locator", "--name", "Key intensity", "--exact")


def save(label):
    step(label, "press", "command + s", "--class", "MotionEditor")


def seek(seconds):
    field = next(node for node in nodes(snapshot()) if node.get("componentName") == "Timeline position")
    step("edit playback position", "click", field["ref"], "--click-count", 2)
    field = next(node for node in nodes(snapshot()) if node.get("componentName") == "Timeline position")
    editor = next(node for node in nodes(field) if node.get("class") == "juce::TextEditor")
    step("seek " + str(seconds), "fill", editor["ref"], str(seconds) + "s")
    step("commit playback position", "press", "Return")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    open_project()
    open_scope()
    save("save baseline")
    if saved().find("beam/booleans/parameter[@id='upsamplingEnabled']").get("value") != "1":
        step("enable upsampling", "click", "--name", "Upsample Audio", "--exact")
    step("set beam intensity", "set-value", intensity()["ref"], "6")
    session.wait_for_undo("Change Intensity")
    step("open scope presets", "click", "--name", "Scope presets", "--exact")
    step("choose laser timing", "click", "--name", "Laser (slow galvo)", "--role", "menuItem", "--exact")
    command("wait-for-locator", "--name", "Undo Apply Laser (slow galvo) scope timing", "--role", "label", "--exact")
    step("set scope dwell", "set-value", scope_field("dwell")["ref"], "80")
    save("save beam settings")
    state = saved()
    assert state.find("beam/booleans/parameter[@id='upsamplingEnabled']").get("value") == "1"
    assert saved_intensity() == (6.0, []), saved_intensity()
    assert state.find("recording/recordingSettings") is not None
    assert saved_scope() == [80, 400, 150], saved_scope()
    # Intensity animates like any property: a key here, a new value later keys there.
    seek(0)
    step("key intensity", "click", "--name", "Key intensity", "--exact")
    session.wait_for_undo("Set keyframe")
    seek(10)
    step("intensity later", "set-value", intensity()["ref"], "2")
    session.wait_for_undo("Change Intensity")
    save("save animated intensity")
    assert saved_intensity()[1] == [(0.0, 6.0), (10.0, 2.0)], saved_intensity()
    step("undo the later key", "click", "--name", "Undo", "--exact")
    save("save after undo")
    assert saved_intensity()[1] == [(0.0, 6.0)], saved_intensity()
    step("redo the later key", "click", "--name", "Redo", "--exact")
    save("save after redo")
    step("change intensity without saving", "set-value", intensity()["ref"], "9")
    step("disable upsampling without saving", "click", "--name", "Upsample Audio", "--exact")
    step("change settle without saving", "set-value", scope_field("settle")["ref"], "0")
    open_project()
    open_scope()
    seek(10)
    assert abs(float(intensity()["value"]) - 2) < .001, intensity()["value"]
    assert abs(float(scope_field("dwell")["value"]) - 80) < .001
    assert abs(float(scope_field("settle")["value"]) - 150) < .001
    step("reopened scope properties", "screenshot", "--file", session.artifact_dir / "beam-settings.png", "--scale", "2")
    # Resave restored runtime state, so this checks restoration rather than merely
    # inspecting the bytes written before changing the controls.
    save("resave restored beam")
    assert saved().find("beam/booleans/parameter[@id='upsamplingEnabled']").get("value") == "1"
    assert saved_scope() == [80, 400, 150], saved_scope()
    assert saved_intensity()[1] == [(0.0, 6.0), (10.0, 2.0)], saved_intensity()
    for moment in [64, 96, 128, 179]:
        field = next(node for node in nodes(snapshot()) if node.get("componentName") == "Timeline position")
        step("edit playback position", "click", field["ref"], "--click-count", 2)
        field = next(node for node in nodes(snapshot()) if node.get("componentName") == "Timeline position")
        editor = next(node for node in nodes(field) if node.get("class") == "juce::TextEditor")
        step("seek " + str(moment), "fill", editor["ref"], str(moment) + "s")
        step("commit playback position", "press", "Return")
        # Clear persistence from the previous shot before comparing static output.
        command("wait", "--ms", "4000")
        step("upsampled frame " + str(moment), "screenshot", "--file", session.artifact_dir / f"upsampled-{moment:03}.png")
    print("Beam settings and scope timing restored after unsaved changes; upsampled output captured for visual review")
finally:
    session.stop_app()
