#!/usr/bin/env python3
"""Timeline usability through the real workspace: loop range and button, snapping toggle,
track resizing, dropping a file onto a track, box selection, edit-point and marker
navigation, the Graph's channel list, Easy Ease and After Effects keying shortcuts."""
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
session.launch_app("motion-timeline-usability")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


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


def find(predicate, tree=None):
    return next((node for node in nodes(tree if tree is not None else snapshot()) if predicate(node)), None)


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


# Three visual tracks: clips at 0-4, 6-10 and 2-8 s; the first clip's weight is keyed.
root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Usability study", duration="20", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Square.obj", extension=".obj")
asset.text = encoded(b"v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nl 1 2 3 4 1\n")
for track_id, clip_id, start, duration in ((10, 11, 0, 4), (20, 21, 6, 4), (30, 31, 2, 6)):
    track = ET.SubElement(composition, "track", id=str(track_id), name=f"Row {track_id // 10}", kind="visual")
    clip = ET.SubElement(track, "clip", id=str(clip_id), asset="1", name=f"Shape {track_id // 10}", start=str(start), duration=str(duration), offset="0", rate="1", timeBase="seconds", contentBpm="120")
    for group, base in (("position", 0), ("rotation", 0), ("scale", 1)):
        for axis in "xyz":
            ET.SubElement(clip, "property", name=f"{group}.{axis}", base=str(base))
    for prop, base in (("red", 1), ("green", 1), ("blue", 1)):
        ET.SubElement(clip, "property", name=prop, base=str(base))
    weight = ET.SubElement(clip, "property", name="weight", base="1")
    if clip_id == 11:
        ET.SubElement(weight, "key", time="0", value="0", interpolation="1")
        ET.SubElement(weight, "key", time="2", value="1", interpolation="1")
ET.SubElement(composition, "camera", id="9", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "usability.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
extra = session.artifact_dir / "Extra.obj"
extra.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nl 1 2 3 1\n")


def saved():
    step("save", "press", "command + s", "--class", "MotionEditor")
    command("wait", "--ms", 300)
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


def seek(seconds):
    field = find(lambda n: n.get("componentName") == "Timeline position" and n.get("visible"))
    step("open position", "click", field["ref"], "--click-count", 2)
    field = find(lambda n: n.get("componentName") == "Timeline position")
    editor = find(lambda n: n.get("class") == "juce::TextEditor", field)
    step(f"position {seconds}", "fill", editor["ref"], f"{seconds}s")
    step("commit position", "press", "Return")


def position():
    return find(lambda n: n.get("componentName") == "Timeline position")["value"]


def timeline_bounds():
    return find(lambda n: n.get("class") == "MotionTimelineView")["bounds"]


def track_row(track_id):
    """Timeline-local y of a track row's top, from its name label."""
    label = find(lambda n: n.get("componentName") == f"Track name {track_id}" and n.get("visible"))
    return label["bounds"]["y"] - timeline_bounds()["y"] - 5


def track(tree, track_id):
    return next(t for t in tree.iter("track") if t.get("id") == str(track_id))


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Usability study", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    step("fit", "press", "F", "--class", "MotionTimelineView")
    command("wait", "--ms", 300)

    # Loop: I and O set the range at the playhead, L switches looping on, and
    # the transport button switches it off.
    seek(2)
    step("loop start", "press", "i", "--class", "MotionTimelineView")
    seek(6)
    step("loop end", "press", "o", "--class", "MotionTimelineView")
    state = saved()
    assert float(state.get("loopStart")) == 2 and float(state.get("loopEnd")) == 6 and state.get("looping") in ("0", "false"), state.attrib
    step("loop on", "press", "l", "--class", "MotionTimelineView")
    assert saved().get("looping") in ("1", "true")
    step("loop button", "click", "--name", "Loop playback", "--class", "motion::style::IconButton", "--exact")
    assert saved().get("looping") in ("0", "false")
    step("loop again", "press", "l", "--class", "MotionTimelineView")
    assert saved().get("looping") in ("1", "true")

    # The magnet switches snapping off and on.
    step("snapping off", "click", "--name", "Snapping", "--exact")
    assert saved().get("gridSnap") == "0"
    step("snapping on", "click", "--name", "Snapping", "--exact")
    assert saved().get("gridSnap") == "1"

    # Resize the first track by its bottom edge, then double-click it back.
    bottom = track_row(10) + 32 - 2
    step("resize track", "drag", "--class", "MotionTimelineView", "--position", f"80,{bottom}", "--dx", 0, "--dy", 30, "--steps", 6)
    assert int(track(saved(), 10).get("height")) == 62, track(saved(), 10).attrib
    step("reset track height", "click", "--class", "MotionTimelineView", "--position", f"80,{track_row(10) + 62 - 2}", "--click-count", 2)
    assert track(saved(), 10).get("height") is None
    step("tracks", "screenshot", "--file", session.artifact_dir / "tracks.png")

    # Drop a file onto the second track's empty stretch at about 13 s.
    area = timeline_bounds()
    pixels = (area["w"] - 170 - 20) / 20
    x = 170 + round(13 * pixels)
    # Jucewright places a file drop relative to the drop target (the editor).
    step("drop on track", "drop-files", "--file", extra, "--class", "MotionEditor", "--exact", "--position", f"{area['x'] + x},{area['y'] + track_row(20) + 16}")
    command("wait-for-locator", "--name", "Undo Import object", "--role", "label", "--exact", "--timeout-ms", 30000)
    state = saved()
    clips = track(state, 20).findall("clip")
    assert len(clips) == 2 and abs(float(clips[1].get("start")) - 13) < 0.6, [c.attrib for c in clips]
    assert len(state.findall("track")) == 3, "the drop did not make a new track"
    step("undo drop", "click", "--name", "Undo", "--exact")

    # Box-select from empty space over the third track, then delete and undo.
    row = track_row(30) + 16
    step("box select", "drag", "--class", "MotionTimelineView", "--position", f"{170 + round(19 * pixels)},{row}", "--dx", -round(18 * pixels), "--dy", 2, "--steps", 8)
    step("delete boxed", "press", "Delete", "--class", "MotionTimelineView")
    assert len(track(saved(), 30).findall("clip")) == 0
    step("undo delete", "click", "--name", "Undo", "--exact")
    assert len(track(saved(), 30).findall("clip")) == 1

    # Up / Down jump between clip edges; J / K to keys and markers.
    seek(0)
    step("next edit", "press", "Down", "--class", "MotionTimelineView")
    command("wait-for-value", "--component-name", "Timeline position", "--value", "2.000s", "--timeout-ms", 5000)
    step("next edit again", "press", "Down", "--class", "MotionTimelineView")
    command("wait-for-value", "--component-name", "Timeline position", "--value", "4.000s", "--timeout-ms", 5000)
    step("previous edit", "press", "Up", "--class", "MotionTimelineView")
    command("wait-for-value", "--component-name", "Timeline position", "--value", "2.000s", "--timeout-ms", 5000)

    # The Graph's channel list: the first clip's weight is keyed; the filter
    # hides the rest.
    seek(1)
    step("select first clip", "click", "--class", "MotionTimelineView", "--position", f"{170 + round(1 * pixels)},{track_row(10) + 16}")
    step("graph", "click", "--name", "Graph", "--class", "MotionTabs::Tab", "--exact")
    command("wait-for-locator", "--name", "Curve Drawing weight", "--role", "listItem", "--selected", "--exact", "--timeout-ms", 5000)
    # Only animated channels are listed until the filter is switched off.
    hidden = find(lambda n: n.get("name") == "Curve Position X")
    assert hidden is None or not hidden.get("visible"), "unanimated channels are listed"
    step("all channels", "click", "--name", "Animated channels only", "--exact")
    shown = find(lambda n: n.get("name") == "Curve Position X")
    assert shown is not None and shown.get("visible"), "switching the filter off lists every channel"
    step("animated only", "click", "--name", "Animated channels only", "--exact")
    step("graph list", "screenshot", "--file", session.artifact_dir / "graph-list.png")

    # Cmd+A in the Graph selects the curve's keys; F9 eases them.
    def keys(name):
        clip = next(c for c in saved().iter("clip") if c.get("id") == "11")
        return [k for p in clip.iter("property") if p.get("name") == name for k in p.findall("key")]

    step("select all keys", "press", "command + a", "--class", "MotionCurveEditor")
    step("easy ease", "press", "F9", "--class", "MotionCurveEditor")
    eased = keys("weight")
    assert eased[0].get("interpolation") == "3" and float(eased[0].get("out")) == 0 and float(eased[1].get("in")) == 0, [k.attrib for k in eased]
    # Alt+Shift+P keys position at the playhead, as in After Effects.
    step("key position", "press", "alt + shift + p", "--class", "MotionCurveEditor")
    keyed = keys("position.x")
    assert len(keyed) == 1 and abs(float(keyed[0].get("time")) - 1) < 1e-6, [k.attrib for k in keyed]
    # The shortcuts reference opens with its filter focused.
    step("shortcuts", "press", "command + /", "--class", "MotionEditor")
    command("wait-for-locator", "--name", "Filter shortcuts", "--exact", "--timeout-ms", 5000)
    step("shortcuts screenshot", "screenshot", "--file", session.artifact_dir / "shortcuts.png")
    step("close shortcuts", "press", "Escape", "--name", "Filter shortcuts", "--class", "juce::TextEditor", "--exact")
    command("wait", "--ms", 800)
    # Right-clicking the Scene offers views and keying for the selection.
    step("scene menu", "right-click", "--class", "MotionCompositionView")
    step("key rotation from scene", "click", "--name", "Key rotation", "--role", "menuItem", "--exact")
    keyed = keys("rotation.x")
    assert len(keyed) == 1 and abs(float(keyed[0].get("time")) - 1) < 1e-6, [k.attrib for k in keyed]
    print("Timeline usability passed.", flush=True)
finally:
    session.stop_app()
