#!/usr/bin/env python3
"""Per-character text animation and animated Lua sliders through the real workspace."""
import json
import struct
import subprocess
import time
import xml.etree.ElementTree as ET
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-text-lua")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def wait_undo(label):
    command("wait-for-locator", "--name", "Undo " + label, "--role", "label", "--exact", "--timeout-ms", 60000)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


project = session.artifact_dir / "sliders.osci-motion"
title = session.artifact_dir / "Title.txt"
title.write_text("HELLO")
radius = session.artifact_dir / "Radius.lua"
radius.write_text("return {slider_a * math.cos(phase), slider_a * math.sin(phase)}\n")


def saved():
    step("save", "press", "command + s", "--class", "MotionEditor")
    command("wait", "--ms", 400)
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    root = ET.Element("motion-project", schema="1")
    composition = ET.SubElement(root, "composition", name="Slider study", duration="8", bpm="120", fps="30")
    ET.SubElement(composition, "camera", id="9", name="Camera")
    xml = ET.tostring(root, encoding="utf-8")
    project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Slider study", "--timeout-ms", 60000)
    # Text: rise in, character by character.
    step("import text", "drop-files", "--file", title, "--class", "MotionEditor", "--exact")
    wait_undo("Import object")
    # Per-character animation lives in the text clip's Properties.
    command("wait-for-locator", "--name", "Text animation", "--exact", "--timeout-ms", 10000)
    step("rise", "set-value", "--name", "Text animation", "--exact", "Rise")
    wait_undo("Animate text")
    step("stagger", "set-value", "--component-name", "Text animation Stagger", "--exact", "0.1")
    command("wait", "--ms", 1500)
    step("text panel", "screenshot", "--file", session.artifact_dir / "text-panel.png")
    typography = saved().find("asset/typography")
    assert typography is not None and typography.get("animation") == "2" and float(typography.get("characterDelay")) == 0.1, typography.attrib if typography is not None else None
    # Lua: bake, then animate slider A from the inspector.
    step("import lua", "drop-files", "--file", radius, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact")
    step("bake", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Radius.lua", "--role", "label", "--exact", "--timeout-ms", 60000)
    command("wait-for-locator", "--component-name", "slider.a", "--timeout-ms", 20000)
    step("slider", "set-value", "--component-name", "slider.a", "0.6")
    wait_undo("Change property")
    tree = None
    for _ in range(60):
        tree = saved()
        if tree.find(".//clip/luaBake") is not None:
            break
        time.sleep(0.5)
    clip = next(c for c in tree.iter("clip") if c.find("luaBake") is not None)
    assert clip.find("property[@name='slider.a']") is not None
    step("sliders", "screenshot", "--file", session.artifact_dir / "sliders.png")
    print("Text animation and Lua sliders passed.", flush=True)
finally:
    session.stop_app()
