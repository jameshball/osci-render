#!/usr/bin/env python3
"""Exercise real macOS MIDI input, sustain and transient clip audition."""
import json
import subprocess
import sys

import numpy as np
from PIL import Image
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

if sys.platform != "darwin":
    raise SystemExit("This external-input smoke test requires macOS CoreMIDI.")
from jucewright_osci_browser.macos_midi import VirtualSource

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("live-midi")
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


def find(class_name, name=None):
    tree = json.loads(command("snapshot", "--json", "--full"))
    return next(node for node in nodes(tree) if node.get("class") == class_name
                and (name is None or node.get("name") == name))


def beam(label):
    path = session.artifact_dir / (label + ".png")
    step(label, "screenshot", "--file", path)
    box = find("VisualiserComponent")["bounds"]
    # Exclude beam toolbar controls and panel edges. Pixel checks establish
    # signal presence/absence, not frequency or perceptual drawing quality.
    picture = Image.open(path).convert("RGB")
    width, height = box.get("width", box.get("w")), box.get("height", box.get("h"))
    region = picture.crop((box["x"] + 20, box["y"] + 20,
                           box["x"] + width - 20, box["y"] + height - 40))
    return int(np.any(np.asarray(region) > 40, axis=2).sum())


source = None
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", "1440", "--h", "900")
    shape = session.artifact_dir / "MIDI diamond.svg"
    shape.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">'
                     '<path fill="none" stroke="white" d="M50 10 L90 50 L50 90 L10 50 Z"/></svg>')
    step("import audition source", "drop-files", "--file", shape, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Undo Import object", "--role", "label", "--exact")
    step("select audition clip", "click", "--class", "MotionTimelineView", "--position", "190,55")
    step("open notes", "click", "--name", "Notes", "--class", "MotionTabs::Tab", "--exact")
    step("enable MIDI audition", "click", "--name", "MIDI audition", "--class", "juce::TextButton", "--exact")
    command("wait", "--ms", "4000")
    silent = beam("waiting-for-input")
    assert silent < 20, silent
    # Desktop standalone keeps newly connected inputs disabled until selected.
    source = VirtualSource("Motion audition test")
    command("wait", "--ms", "1500")
    step("open input settings", "select-option", "--role", "menuBar", "--text", "Settings...")
    device_list = "juce::CustomAudioDeviceSelectorComponent::CustomMidiInputSelectorComponentListBox"
    command("wait-for-locator", "--class", device_list, "--exact")
    # Select the native list row and use its Return-key toggle action.
    step("select virtual MIDI input", "select-option", "--class", device_list,
         "--exact", "--index", str(source.source_index()))
    step("enable virtual MIDI input", "press", "Return", "--class", device_list, "--exact")
    step("selected input settings", "screenshot", "--file", session.artifact_dir / "selected-input.png")
    step("close input settings", "click", "--name", "Close icon", "--exact")
    source.send([0x90, 69, 127])
    command("wait", "--ms", "1800")
    held = beam("live-note")
    assert held > 100, held
    source.send([0xB0, 64, 127])
    source.send([0x80, 69, 0])
    command("wait", "--ms", "1000")
    sustained = beam("sustained-note")
    assert sustained > 100, sustained
    source.send([0xB0, 64, 0])
    command("wait", "--ms", "4000")
    released = beam("released-note")
    assert released < 20, released
    step("return to timeline", "click", "--name", "Timeline", "--class", "MotionTabs::Tab", "--exact")
    step("reopen notes", "click", "--name", "Notes", "--class", "MotionTabs::Tab", "--exact")
    toggle = find("juce::TextButton", "MIDI audition")
    assert not toggle.get("checked", False), toggle
    command("wait", "--ms", "1800")
    restored = beam("normal-composition-restored")
    assert restored > 100, restored
    command("wait-for-locator", "--name", "Create notes", "--class", "juce::TextButton", "--exact")
    report = dict(waiting=silent, held=held, sustained=sustained, released=released, restored=restored)
    (session.artifact_dir / "live-midi-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Real CoreMIDI input, sustain/release and audition exit passed:", report)
finally:
    if source is not None:
        source.send([0xB0, 64, 0])
        source.send([0x80, 69, 0])
        source.close()
    session.stop_app()
