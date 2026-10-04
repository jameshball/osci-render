#!/usr/bin/env python3
"""Tempo from the soundtrack, tap tempo, a MIDI file's tempo, ramps and ruler-format timing through the real workspace."""
import json
import math
import re
import random
import struct
import subprocess
import time
import wave
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
session.launch_app("motion-tempo-tools")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def wait_undo(text, timeout=30000):
    command("wait-for-locator", "--name", "Undo " + text, "--role", "label", "--exact", "--timeout-ms", timeout)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def groove(path, bpm, offset, seconds):
    """A 48 kHz drum loop: kick on each bar's downbeat, snare on 2 and 4, hats on every beat."""
    rate = 48000
    samples = [0.0] * int(seconds * rate)
    rng = random.Random(3)
    beat = 60 / bpm
    index = 0
    while offset + index * beat < seconds:
        start = int((offset + index * beat) * rate)
        for i in range(6000):
            if start + i >= len(samples):
                break
            t = i / rate
            value = 0.3 * (rng.random() * 2 - 1) * math.exp(-t * 90)
            if index % 4 == 0:
                value += 0.9 * math.sin(2 * math.pi * 55 * t) * math.exp(-t * 18)
            if index % 4 in (1, 3):
                value += 0.5 * (rng.random() * 2 - 1) * math.exp(-t * 30)
            samples[start + i] += value
        index += 1
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(b"".join(struct.pack("<h", max(-32767, min(32767, int(v * 20000)))) for v in samples))


def midi_with_tempo(path):
    """One note; 100 BPM, then 140 BPM from beat 4."""
    def vlq(value):
        out = [value & 0x7f]
        value >>= 7
        while value:
            out.append((value & 0x7f) | 0x80)
            value >>= 7
        return bytes(reversed(out))
    track = vlq(0) + bytes([0xff, 0x51, 3]) + (600000).to_bytes(3, "big")
    track += vlq(0) + bytes([0x90, 60, 100]) + vlq(480) + bytes([0x80, 60, 0])
    track += vlq(3 * 480) + bytes([0xff, 0x51, 3]) + int(60000000 / 140).to_bytes(3, "big")
    track += vlq(0) + bytes([0xff, 0x2f, 0])
    path.write_bytes(b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big") + (480).to_bytes(2, "big")
                     + b"MTrk" + len(track).to_bytes(4, "big") + track)


project = session.artifact_dir / "tempo-tools.osci-motion"
root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Tempo tools", duration="12", bpm="100", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
loop = session.artifact_dir / "loop.wav"
groove(loop, 128, 0.3, 12)
melody = session.artifact_dir / "Tempo.mid"
midi_with_tempo(melody)


def saved():
    step("save", "press", "command + s", "--class", "MotionEditor")
    command("wait", "--ms", 300)
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Tempo tools", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    # Detect tempo and downbeat from a 128 BPM loop whose first downbeat is 0.3 s in.
    step("import loop", "drop-files", "--file", loop, "--class", "MotionEditor", "--exact")
    wait_undo("Import soundtrack", 60000)
    step("timing menu", "click", "--name", "Timing", "--class", "juce::MenuBarComponent::AccessibleItemComponent", "--exact")
    step("detect", "click", "--name", "Detect tempo from soundtrack", "--role", "menuItem", "--exact")
    wait_undo("Set tempo from soundtrack", 60000)
    tree = saved()
    assert abs(float(tree.get("bpm")) - 128) < 0.05, tree.attrib
    clip = tree.find("track/clip")
    bar = 4 * 60 / 128
    # Moved later so the 0.3 s downbeat lands on the next bar line.
    assert abs(float(clip.get("start")) + 0.3 - bar) < 0.03, clip.attrib
    step("detected", "screenshot", "--file", session.artifact_dir / "detected.png")
    # Detection switches the ruler to bars in the same step, and the clip's
    # timing (in Properties) then shows bars.beats.
    assert tree.get("timeDisplay") == "2", tree.attrib
    step("select soundtrack", "click", "--class", "MotionTimelineView", "--position", "450,64")
    command("wait", "--ms", 300)
    tree = json.loads(command("snapshot", "--json", "--full"))
    start = next((node for node in nodes(tree) if node.get("componentName") == "Clip start" and node.get("visible")), None)
    label = str(start.get("value", start.get("text", start.get("name")))) if start else ""
    # 1.575 s at 128 BPM is beat 3.36: bar 1, beat 4.
    assert re.fullmatch(r"1\.4\.\d{3}", label) or "1.4." in json.dumps(start), start
    step("timing", "screenshot", "--file", session.artifact_dir / "timing.png")
    # Tap tempo: five taps half a second apart, committed after a pause.
    for index in range(5):
        step(f"tap {index}", "click", "--name", "Tap tempo", "--exact")
        time.sleep(0.45)
    wait_undo("Change tempo", 10000)
    bpm = float(saved().get("bpm"))
    assert 60 <= bpm <= 200 and abs(bpm - 128) > 0.5, bpm
    # A MIDI file's tempo map replaces the project's.
    step("import midi", "drop-files", "--file", melody, "--class", "MotionEditor", "--exact")
    wait_undo("Import MIDI file", 60000)
    step("midi menu", "click", "--name", "Tempo.mid", "--role", "listItem", "--exact", "--button", "right")
    step("use tempo", "click", "--name", "Use this file's tempo (100 BPM, 1 change)", "--role", "menuItem", "--exact")
    wait_undo("Use MIDI tempo")
    tree = saved()
    change = tree.find("tempo")
    assert float(tree.get("bpm")) == 100 and change is not None and float(change.get("beat")) == 4 and abs(float(change.get("bpm")) - 140) < 1e-6, ET.tostring(tree)
    # A new tempo change that glides in from the previous tempo.
    step("ruler menu", "click", "--class", "MotionTimelineView", "--position", "950,12", "--button", "right")
    step("add tempo", "click", "--name", "Add tempo change here...", "--role", "menuItem", "--exact")
    command("wait-for-locator", "--name", "Tempo change BPM", "--timeout-ms", 10000)
    step("bpm", "fill", "--name", "Tempo change BPM", "--class", "juce::TextEditor", "--exact", "90")
    step("ramp", "click", "--name", "Ramp into tempo", "--exact")
    step("apply", "press", "Return", "--name", "Tempo change BPM", "--class", "juce::TextEditor", "--exact")
    wait_undo("Add tempo change")
    changes = saved().findall("tempo")
    assert any(item.get("ramp") in ("1", "true") and float(item.get("bpm")) == 90 for item in changes), [item.attrib for item in changes]
    step("tempo lane", "screenshot", "--file", session.artifact_dir / "tempo-lane.png")
    print("Tempo tools passed.", flush=True)
finally:
    session.stop_app()
