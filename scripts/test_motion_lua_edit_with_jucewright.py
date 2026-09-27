#!/usr/bin/env python3
"""Verify shared Lua editing, runtime-error recovery, undo and reopen through the UI.

This is a usability stress pass, not the finished music-video benchmark.
"""
import json
import struct
import subprocess
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
session.launch_app("motion-lua-edit")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Lua editing study", duration="180", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "lua-edit.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def saved_clips():
    step("save selection test", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).findall("./composition/track/clip")


def saved_asset():
    saved_clips()
    data = project.read_bytes()
    root = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    return ET.tostring(root.find("./composition/asset"))

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    source = session.artifact_dir / "Orbit.lua"
    original = "return {0.7*math.cos(phase),0.7*math.sin(phase),0,1,0.3,0.1}"
    revised = "return {0.7*math.cos(phase),0.3*math.sin(phase),0,0.2,0.7,1}"
    broken = 'error("Keep this draft")'
    source.write_text(original)
    step("import Lua", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact")
    step("short source cache", "fill", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact", "1")
    step("bake initial source", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Undo Import object", "--role", "label", "--exact")
    step("duplicate shared source", "press", "command + d", "--class", "MotionTimelineView")
    before = saved_asset()
    assert len(saved_clips()) == 2
    step("open Lua source", "click", "--name", "Edit Lua...", "--exact")
    step("introduce runtime error", "fill", "--name", "Lua Code Editor", "--role", "editableText", "--exact", broken)
    step("bake invalid draft", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--component-name", "Source preparation error", "--role", "label", "--exact")
    snapshot = command("snapshot", "--json", "--full")
    assert any(node.get("componentName") == "Lua Code Editor" and node.get("role") == "editableText" and node.get("value") == broken for node in nodes(json.loads(snapshot)))
    step("error and retained draft", "screenshot", "--file", session.artifact_dir / "lua-error.png")
    step("correct retained source", "fill", "--name", "Lua Code Editor", "--role", "editableText", "--exact", revised)
    step("source editor screenshot", "screenshot", "--file", session.artifact_dir / "lua-editor.png")
    step("bake corrected source", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Undo Edit Lua source", "--role", "label", "--exact")
    after = saved_asset()
    clips = saved_clips()
    assert after != before and len(clips) == 2 and clips[0].get("asset") == clips[1].get("asset")
    step("undo successful edit", "click", "--name", "Undo", "--exact")
    assert saved_asset() == before
    step("redo successful edit", "click", "--name", "Redo", "--exact")
    assert saved_asset() == after
    step("open source for cancellation", "click", "--name", "Edit Lua...", "--exact")
    step("unapplied edit", "fill", "--name", "Lua Code Editor", "--role", "editableText", "--exact", "return {0,0}")
    step("discard unapplied draft", "click", "--name", "Close icon", "--exact")
    assert saved_asset() == after
    session.keep_app = False
    session.launch_app("lua-reopen")
    session.keep_app = keep
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait", "--ms", 500)
    step("select saved source", "click", "--name", "Orbit.lua", "--role", "listItem", "--exact", "--position", "6,12")
    step("reopen saved code", "click", "--name", "Edit Lua...", "--exact")
    snapshot = command("snapshot", "--json", "--full")
    assert revised in snapshot and "Keep this draft" not in snapshot
    step("reopened editor", "screenshot", "--file", session.artifact_dir / "lua-reopened.png")
    step("close editor", "click", "--name", "Close icon", "--exact")
    step("edited Lua output", "screenshot", "--file", session.artifact_dir / "lua-output.png")
    print("Shared Lua editing, failed draft recovery, undo/redo, cancel and reopen passed", flush=True)
finally:
    session.stop_app()
