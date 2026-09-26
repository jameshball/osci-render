#!/usr/bin/env python3
"""Verify authored beam settings and inspect the benchmark with upsampling enabled.

Requires the UI-authored Phase / Space benchmark. Override its location with
MOTION_BENCHMARK_PROJECT. Only a copy in the automation artifact directory changes.
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
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", "/private/tmp/phase-space-polished-reviewed/Phase Space polished.osci-motion"))
project = session.artifact_dir / "Phase Space beam.osci-motion"
shutil.copyfile(source, project)
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
    return next(node for node in nodes(snapshot()) if node.get("name") == "Line Intensity" and node.get("class") == "juce::Slider")


def saved():
    data = project.read_bytes()
    assert struct.unpack_from("<I", data)[0] == 0x21324356
    length = struct.unpack_from("<I", data, 4)[0]
    return ET.fromstring(data[8:8 + length])


def open_project():
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-locator", "--name", "Hero diamond.obj", "--class", "juce::Label", "--exact")


def open_settings():
    step("open beam settings", "click", "--name", "settings", "--class", "osci::SvgButton", "--exact")


def close_settings():
    step("close beam settings", "press", "Escape", "--name", "Beam settings", "--class", "SettingsWindow", "--exact")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    open_project()
    open_settings()
    step("save baseline", "press", "Command+s", "--name", "Beam settings", "--class", "SettingsWindow", "--exact")
    if saved().find("beam/booleans/parameter[@id='upsamplingEnabled']").get("value") != "1":
        step("enable upsampling", "click", "--name", "Upsample Audio", "--exact")
    step("set beam intensity", "set-value", intensity()["ref"], "6")
    step("save beam settings", "press", "Command+s", "--name", "Beam settings", "--class", "SettingsWindow", "--exact")
    state = saved()
    assert state.find("beam/booleans/parameter[@id='upsamplingEnabled']").get("value") == "1"
    assert float(state.find("beam/effects/parameter[@id='intensity']/parameter").get("value")) == 6
    assert state.find("recording/recordingSettings") is not None
    step("change intensity without saving", "set-value", intensity()["ref"], "2")
    step("disable upsampling without saving", "click", "--name", "Upsample Audio", "--exact")
    close_settings()
    open_project()
    open_settings()
    assert abs(float(intensity()["value"]) - 6) < .001
    step("reopened beam settings", "screenshot", "--name", "Beam settings", "--class", "SettingsWindow", "--exact", "--file", session.artifact_dir / "beam-settings.png")
    # Resave restored runtime state, so this checks restoration rather than merely
    # inspecting the bytes written before changing the controls.
    step("resave restored beam", "press", "Command+s", "--name", "Beam settings", "--class", "SettingsWindow", "--exact")
    assert saved().find("beam/booleans/parameter[@id='upsamplingEnabled']").get("value") == "1"
    close_settings()
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
    print("Beam settings restored after unsaved changes; upsampled output captured for visual review")
finally:
    session.stop_app()
