#!/usr/bin/env python3
"""Add narrative navigation cues to a copy of Return Path through Motion's UI.

These are editorial landmarks, not a claim of final musical phrase alignment.
The source arrangement and rendered movie remain unchanged.
"""
import os
import json
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
source = Path(os.environ.get("MOTION_BENCHMARK_PROJECT", str(session.root_dir / "artifacts/return-path/bridge-study/Return Path bridge.osci-motion")))
project = session.artifact_dir / "Return Path cues.osci-motion"
shutil.copyfile(source, project)


def read_project(path):
    data = path.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])


original = read_project(source)
composition_name = original.find("composition").get("name")
cues = [(0, "Wake"), (12.8, "Anyone there?"), (25.6, "First route"),
        (51.2, "Overload"), (76.8, "Alone"), (102.4, "The reply"),
        (128, "Build the bridge"), (153.6, "Connection"), (179.2, "Still here")]
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("return-path-cues")
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


def seek(time):
    step("edit cue playhead", "click", "--component-name", "Timeline position", "--click-count", 2)
    tree = json.loads(command("snapshot", "--json", "--full"))
    label = find(tree, lambda n: n.get("componentName") == "Timeline position")
    field = find(label, lambda n: n.get("class") == "juce::TextEditor")
    assert field is not None
    step("seek cue", "fill", field["ref"], str(time) + "s")
    step("commit cue playhead", "press", "Return")
    command("wait-for-value", "--component-name", "Timeline position", "--value", f"{time:.3f}s", "--timeout-ms", 5000)


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", composition_name, "--timeout-ms", 60000)
    step("size workspace", "resize-window", "--w", 1600, "--h", 1000)
    for time, name in cues:
        seek(time)
        step("add cue " + name, "press", "m", "--class", "MotionTimelineView")
        step("cue name", "fill", "--name", "Marker name", "--class", "juce::TextEditor", "--exact", name)
        step("cue position", "fill", "--name", "Marker position", "--class", "juce::TextEditor", "--exact", str(time) + "s")
        step("save cue", "click", "--name", "Save marker", "--exact")
        command("wait-for-locator", "--name", "Undo Add marker", "--role", "label", "--exact")
    step("save editorial cues", "press", "command + s", "--class", "MotionEditor")
    saved = read_project(project)
    assert [(float(m.get("time")), m.get("name")) for m in saved.findall("composition/marker")] == cues
    # Cues should leave every authored clip, asset, camera and effect untouched.
    for root in (original, saved):
        root.attrib.pop("projectFilePath", None)
        # Earlier source files predate explicit default typography serialization.
        # Ignore only the exact defaults, preserving the embedded source text.
        for asset in root.findall("composition/asset"):
            typography = asset.find("typography")
            if typography is not None and typography.attrib == {"family": "", "style": "0", "alignment": "0", "lineSpacing": "1.2", "tracking": "0"}:
                asset.text = (asset.text or "") + (typography.tail or "")
                asset.remove(typography)
        composition = root.find("composition")
        for marker in list(composition.findall("marker")):
            composition.remove(marker)
    assert ET.canonicalize(ET.tostring(original, encoding="unicode"), strip_text=True) == ET.canonicalize(ET.tostring(saved, encoding="unicode"), strip_text=True)
    step("fit whole story", "press", "f", "--class", "MotionTimelineView")
    step("editorial cue overview", "screenshot", "--file", session.artifact_dir / "story-cues.png")
    print("Nine narrative cues saved; non-marker project content unchanged", flush=True)
finally:
    session.stop_app()
