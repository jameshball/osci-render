#!/usr/bin/env python3
"""Verify shared text editing, cancellation, undo and reopen through the UI.

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
session.launch_app("motion-text-edit")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Text editing study", duration="180", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "text-edit.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")

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
    source = session.artifact_dir / "Title.txt"
    source.write_text("ORIGINAL")
    step("import text", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Title.txt", "--class", "juce::Label", "--exact")
    step("duplicate text clip", "press", "command + d", "--class", "MotionTimelineView")
    before = saved_asset()
    clips = saved_clips()
    assert len(clips) == 2 and clips[0].get("asset") == clips[1].get("asset")
    step("open shared text", "click", "--name", "Edit text...", "--exact")
    command("wait-for-locator", "--name", "Source text", "--class", "juce::TextEditor", "--exact")
    step("edit multiline text", "fill", "--name", "Source text", "--class", "juce::TextEditor", "--exact", "RETURN\nTOGETHER")
    step("choose font", "select-option", "--name", "Text font family", "--class", "juce::ComboBox", "--exact", "--text", "Menlo")
    step("choose bold", "select-option", "--name", "Text font style", "--class", "juce::ComboBox", "--exact", "--text", "Bold")
    step("centre title", "select-option", "--name", "Text alignment", "--class", "juce::ComboBox", "--exact", "--text", "Centre")
    step("set line spacing", "set-value", "--name", "Text line spacing", "--role", "slider", "1.8")
    step("set tracking", "set-value", "--name", "Text tracking", "--role", "slider", "0.15")
    step("text editor", "screenshot", "--file", session.artifact_dir / "text-editor.png")
    step("apply text", "click", "--name", "Apply text", "--exact")
    command("wait-for-locator", "--name", "Undo Edit text source", "--class", "juce::Label", "--exact")
    after = saved_asset()
    assert after != before and len(saved_clips()) == 2
    typography = ET.fromstring(after).find("typography")
    assert typography.get("family") == "Menlo" and typography.get("style") == "1" and typography.get("alignment") == "1"
    assert float(typography.get("lineSpacing")) == 1.8 and float(typography.get("tracking")) == 0.15
    step("open text for preparation failure", "click", "--name", "Edit text...", "--exact")
    oversized_geometry = "MW" * 8000
    step("draft exceeding geometry budget", "fill", "--name", "Source text", "--class", "juce::TextEditor", "--exact", oversized_geometry)
    step("apply excessive geometry", "click", "--name", "Apply text", "--exact")
    command("wait-for-locator", "--component-name", "Text preparation error", "--role", "label", "--exact", "--timeout-ms", 15000)
    state = json.loads(command("snapshot", "--json", "--full"))
    def nodes(value):
        if isinstance(value, dict):
            yield value
            for child in value.values():
                yield from nodes(child)
        elif isinstance(value, list):
            for child in value:
                yield from nodes(child)
    fields = list(nodes(state))
    assert any(node.get("componentName") == "Source text" and node.get("value") == oversized_geometry for node in fields)
    assert any(node.get("componentName") == "Text font family" and node.get("value") == "Menlo" for node in fields)
    assert any(node.get("componentName") == "Text preparation error" and "too detailed" in str(node.get("value")) for node in fields)
    step("recover shorter title", "fill", "--name", "Source text", "--class", "juce::TextEditor", "--exact", "RECOVERED\nTITLE")
    step("retained formatting and error", "screenshot", "--file", session.artifact_dir / "text-recovery.png")
    step("apply recovered title", "click", "--name", "Apply text", "--exact")
    command("wait-for-locator", "--name", "Edit text...", "--exact")
    command("wait", "--ms", 500)
    recovered = saved_asset()
    assert recovered != after and ET.fromstring(recovered).find("typography").attrib == typography.attrib
    step("undo recovered title", "click", "--name", "Undo", "--exact")
    assert saved_asset() == after
    step("undo shared edit", "click", "--name", "Undo", "--exact")
    assert saved_asset() == before
    step("redo shared edit", "click", "--name", "Redo", "--exact")
    assert saved_asset() == after
    step("open text to cancel", "click", "--name", "Edit text...", "--exact")
    step("unapplied edit", "fill", "--name", "Source text", "--class", "juce::TextEditor", "--exact", "DO NOT APPLY")
    step("dismiss edit", "click", "--name", "Close icon", "--exact")
    assert saved_asset() == after
    # Launch a fresh process so this verifies persisted source content.
    session.keep_app = False
    session.launch_app("text-reopen")
    session.keep_app = keep
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait", "--ms", 500)
    step("select text asset", "click", "--name", "Title.txt", "--role", "listItem", "--exact", "--position", "6,12")
    step("reopen saved text", "click", "--name", "Edit text...", "--exact")
    snapshot = command("snapshot", "--json", "--full")
    assert "RETURN" in snapshot and "TOGETHER" in snapshot and "DO NOT APPLY" not in snapshot
    step("close verified text", "click", "--name", "Close icon", "--exact")
    command("wait", "--ms", 600)
    assert len(saved_clips()) == 2
    step("edited text output", "screenshot", "--file", session.artifact_dir / "text-output.png")
    shared = saved_asset()
    step("open second clip menu", "click", "--class", "MotionTimelineView", "--position", "600,46", "--button", "right")
    step("make second source unique", "click", "--name", "Make this clip's source unique", "--role", "menuItem", "--exact")
    command("wait-for-locator", "--name", "Undo Make source unique", "--class", "juce::Label", "--exact")
    isolated_clips = saved_clips()
    assert len(isolated_clips) == 2 and isolated_clips[0].get("asset") != isolated_clips[1].get("asset")
    step("edit isolated text", "click", "--name", "Edit text...", "--exact")
    step("replace isolated text", "fill", "--name", "Source text", "--class", "juce::TextEditor", "--exact", "ONLY THIS CLIP")
    step("apply isolated text", "click", "--name", "Apply text", "--exact")
    command("wait-for-locator", "--name", "Undo Edit text source", "--class", "juce::Label", "--exact")
    assert saved_asset() == shared
    step("undo isolated edit", "click", "--name", "Undo", "--exact")
    step("undo source separation", "click", "--name", "Undo", "--exact")
    reunited = saved_clips()
    assert reunited[0].get("asset") == reunited[1].get("asset")
    assert saved_asset() == shared
    print("Shared and unique text editing, undo/redo, cancel and reopen passed", flush=True)
finally:
    session.stop_app()
