#!/usr/bin/env python3
"""Exercise note authoring, undo, MIDI asset assignment, save and reopen."""
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
session.launch_app("motion-notes")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def find(value, name):
    if isinstance(value, dict):
        if value.get("componentName") == name:
            return value
        for child in value.values():
            result = find(child, name)
            if result is not None:
                return result
    elif isinstance(value, list):
        for child in value:
            result = find(child, name)
            if result is not None:
                return result
    return None


def tempo(value):
    tree = json.loads(command("snapshot", "--json", "--full"))
    target = find(tree, "Project tempo")
    step("set tempo " + str(value), "fill", target["ref"], str(value))
    step("commit tempo", "press", "Return")


# JUCE MemoryBlock's length-prefixed little-bit-order encoding, used by projects.
def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="MIDI beam verification", duration="8", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="MIDI triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Musical beam", kind="visual")
clip = ET.SubElement(track, "clip", id="3", asset="1", name="Triangle notes", start="0", duration="16", offset="0", rate="1", timeBase="beats", contentBpm="120")
for group, base in [("position", 0), ("rotation", 0), ("scale", 1)]:
    for axis in "xyz":
        ET.SubElement(clip, "property", name=group + "." + axis, base=str(base))
for name, base in [("red", .2), ("green", 1), ("blue", .35), ("weight", 1)]:
    ET.SubElement(clip, "property", name=name, base=str(base))
ET.SubElement(composition, "camera", id="5", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
fixture = session.artifact_dir / "midi-beam.osci-motion"
fixture.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
def saved_notes():
    step("save project", "press", "command + s", "--class", "MotionEditor")
    data = fixture.read_bytes()
    length = struct.unpack("<I", data[4:8])[0]
    saved = ET.fromstring(data[8:8 + length])
    return saved, saved.findall("./composition/track/clip/midi/note")


def drag(label, start, finish):
    tree = json.loads(command("snapshot", "--json", "--full"))
    target = find(tree, "MIDI notes editor")
    box = target.get("bounds", target.get("box"))
    if isinstance(box, dict):
        x, y = box["x"], box["y"]
    else:
        raise RuntimeError("Unexpected notes editor bounds: " + str(box))
    step(label, "drag-xy", str(x + start[0]), str(y + start[1]), str(x + finish[0]), str(y + finish[1]), "--steps", "8")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(fixture)
    command("wait-for-locator", "--name", "Musical beam", "--class", "juce::Label", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("select visual clip", "click", "--class", "MotionTimelineView", "--position", "240,77")
    step("open notes workspace", "click", "--name", "Notes", "--class", "osci::TabBar::Tab", "--exact")
    # Give the piano roll the room it used to take for itself.
    step("taller notes", "drag", "--name", "Resize timeline", "--class", "MotionDivider", "--exact", "--position", "20,3", "--dx", 0, "--dy", -64)
    step("create note pattern", "click", "--name", "Create notes", "--exact")
    step("draw first note", "click", "--class", "MotionNotesEditor", "--position", "222,170", "--click-count", "2")
    command("wait-for-locator", "--name", "Undo Add MIDI note", "--role", "label", "--exact")
    step("draw second note", "click", "--class", "MotionNotesEditor", "--position", "542,138", "--click-count", "2")
    saved, notes = saved_notes()
    assert len(notes) == 2, [note.attrib for note in notes]
    step("authored notes", "screenshot", "--file", session.artifact_dir / "authored.png")
    step("select velocity target", "click", "--class", "MotionNotesEditor", "--position", "234,170")
    velocity = find(json.loads(command("snapshot", "--json", "--full")), "Note velocity")
    step("set note velocity", "fill", velocity["ref"], "55")
    step("commit note velocity", "press", "Return")
    saved, changed = saved_notes()
    assert next(n for n in changed if n.get("id") == "1").get("velocity") == "55"
    step("select mixed velocities", "press", "command + a", "--class", "MotionNotesEditor")
    command("wait-for-locator", "--name", "Mixed", "--class", "juce::Label", "--exact")
    step("mixed velocities layout", "screenshot", "--file", session.artifact_dir / "mixed-velocities.png")
    step("undo velocity", "click", "--name", "Undo", "--exact")
    step("clear mixed selection", "click", "--class", "MotionNotesEditor", "--position", "180,100")
    step("select single gesture target", "click", "--class", "MotionNotesEditor", "--position", "234,170")
    drag("resize first note", (259, 170), (339, 170))
    saved, resized = saved_notes()
    assert abs(float(next(n for n in resized if n.get("id") == "1").get("duration")) - .75) < 1e-9
    assert next(n for n in resized if n.get("id") == "2").attrib == next(n for n in notes if n.get("id") == "2").attrib
    step("undo note resize", "click", "--name", "Undo", "--exact")
    drag("move first note", (234, 170), (314, 154))
    saved, moved = saved_notes()
    first = next(note for note in moved if note.get("id") == "1")
    assert abs(float(first.get("start")) - 1.5) < 1e-9 and first.get("pitch") == "66", first.attrib
    assert next(n for n in moved if n.get("id") == "2").attrib == next(n for n in notes if n.get("id") == "2").attrib
    step("undo note move", "click", "--name", "Undo", "--exact")
    saved, restored = saved_notes()
    assert [(n.get("start"), n.get("pitch")) for n in restored] == [(n.get("start"), n.get("pitch")) for n in notes]
    step("select first note", "click", "--class", "MotionNotesEditor", "--position", "234,170")
    step("delete selected note", "press", "Delete", "--class", "MotionNotesEditor")
    saved, remaining = saved_notes()
    assert len(remaining) == 1
    step("undo note deletion", "click", "--name", "Undo", "--exact")
    step("select all notes", "press", "command + a", "--class", "MotionNotesEditor")
    step("nudge selected notes", "press", "Right", "--class", "MotionNotesEditor")
    saved, nudged = saved_notes()
    assert [float(n.get("start")) for n in nudged] == [1.25, 3.25]
    step("undo note nudge", "click", "--name", "Undo", "--exact")
    # One quarter-note SMF at 480 PPQ, with explicit note-off and end-of-track.
    events = bytes.fromhex("00903c648360803c0000ff2f00")
    midi = session.artifact_dir / "Imported notes.mid"
    midi.write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + b"MTrk" + struct.pack(">I", len(events)) + events)
    step("import MIDI asset", "drop-files", "--file", midi, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Assign to selected clip", "--exact")
    step("assign imported notes", "click", "--name", "Assign to selected clip", "--exact")
    saved, assigned = saved_notes()
    assert len(assigned) == 1 and assigned[0].get("pitch") == "60"
    assert len(saved.findall("./composition/track")) == 1, "MIDI import created an empty visual track"
    step("assigned piano roll", "screenshot", "--file", session.artifact_dir / "assigned.png")
    step("remove MIDI performance", "click", "--name", "Remove MIDI", "--exact")
    saved, removed = saved_notes()
    assert not saved.findall("./composition/track/clip/midi")
    step("undo MIDI removal", "click", "--name", "Undo", "--exact")
    saved, restored = saved_notes()
    assert len(restored) == 1 and restored[0].get("pitch") == "60"
    step("compact notes workspace", "resize-window", "--w", 1100, "--h", 700)
    step("compact layout", "screenshot", "--file", session.artifact_dir / "compact.png")
    session.open_project(fixture)
    step("show reopened timeline", "click", "--name", "Timeline", "--class", "osci::TabBar::Tab", "--exact")
    command("wait-for-locator", "--name", "Musical beam", "--class", "juce::Label", "--exact")
    # A locked track retains readable notes without accepting edits.
    saved.find("./composition/track").set("locked", "1")
    saved.find("./composition/track").set("name", "Locked musical beam")
    locked_xml = ET.tostring(saved, encoding="utf-8")
    locked_fixture = session.artifact_dir / "locked-notes.osci-motion"
    locked_fixture.write_bytes(struct.pack("<II", 0x21324356, len(locked_xml)) + locked_xml + b"\0")
    session.open_project(locked_fixture)
    command("wait-for-locator", "--name", "Locked musical beam", "--class", "juce::Label", "--exact")
    step("select locked visual clip", "click", "--class", "MotionTimelineView", "--position", "240,77")
    step("inspect locked notes", "click", "--name", "Notes", "--class", "osci::TabBar::Tab", "--exact")
    command("wait-for-locator", "--name", "Remove MIDI", "--exact")
    step("select locked notes", "press", "command + a", "--class", "MotionNotesEditor")
    locked_tree = json.loads(command("snapshot", "--json", "--full"))
    assert find(locked_tree, "Note velocity")["enabled"] is False
    assert find(locked_tree, "Remove MIDI")["enabled"] is False
    step("attempt locked delete", "press", "Delete", "--class", "MotionNotesEditor")
    step("attempt locked transpose", "press", "Up", "--class", "MotionNotesEditor")
    step("save locked project", "press", "command + s", "--class", "MotionEditor")
    locked_data = locked_fixture.read_bytes()
    locked_saved = ET.fromstring(locked_data[8:8 + struct.unpack("<I", locked_data[4:8])[0]])
    assert [n.attrib for n in locked_saved.findall("./composition/track/clip/midi/note")] == [n.attrib for n in restored]
    step("locked notes layout", "screenshot", "--file", session.artifact_dir / "locked.png")
    print("Notes authoring/import/assignment/save/read-only workflow passed.", flush=True)
finally:
    session.stop_app()
