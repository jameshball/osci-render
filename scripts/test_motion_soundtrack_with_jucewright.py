#!/usr/bin/env python3
"""Exercise soundtrack import, waveform lanes, gain keys and output routing."""
import json
import math
import os
import struct
import wave
import subprocess
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-soundtrack")
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
            found = find(child, predicate)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find(child, predicate)
            if found is not None:
                return found
    return None


def snapshot():
    return json.loads(command("snapshot", "--json", "--full"))


def wait_undo(text):
    session.wait_for_undo(text)


# A locally generated stereo fixture avoids external media dependencies.
fixture = session.artifact_dir / "soundtrack.wav"
with wave.open(str(fixture), "wb") as output:
    output.setnchannels(2)
    output.setsampwidth(2)
    output.setframerate(48000)
    samples = bytearray()
    for frame in range(6 * 48000):
        time = frame / 48000
        envelope = 0.2 + 0.7 * math.exp(-8 * (time % 0.5))
        samples.extend(struct.pack("<hh", int(16000 * envelope * math.sin(2 * math.pi * 220 * time)), int(12000 * envelope * math.sin(2 * math.pi * 330 * time))))
    output.writeframes(samples)

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1100, "--h", 800)
    step("import visual", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    wait_undo("Import object")
    step("import soundtrack", "drop-files", "--file", fixture, "--class", "MotionEditor", "--exact")
    wait_undo("Import soundtrack")
    command("wait-for-locator", "--name", "Property inspector", "--exact")
    gain = find(snapshot(), lambda node: node.get("componentName") == "gain" and node.get("visible"))
    if gain is None or float(gain["value"]) != 1:
        raise RuntimeError("Audio gain inspector missing or incorrect")
    step("reject audio onto visual lane", "drag", "--class", "MotionTimelineView", "--position", "300,108", "--dx", 0, "--dy", -40)
    wait_undo("Import soundtrack")
    step("move soundtrack", "drag", "--class", "MotionTimelineView", "--position", "300,108", "--dx", 70, "--dy", 0)
    wait_undo("Move clip")
    step("undo soundtrack move", "click", "--name", "Undo", "--exact")
    step("waveform workspace screenshot", "screenshot", "--file", session.artifact_dir / "soundtrack-workspace.png")
    step("seek into soundtrack", "click", "--class", "MotionTimelineView", "--position", "325,12")
    step("key soundtrack gain", "click", "--name", "Key gain", "--exact")
    wait_undo("Set keyframe")
    gain = find(snapshot(), lambda node: node.get("componentName") == "gain" and node.get("visible"))
    step("edit soundtrack gain", "set-value", gain["ref"], "1.25")
    wait_undo("Change property")
    step("undo gain change", "click", "--name", "Undo", "--exact")
    gain = find(snapshot(), lambda node: node.get("componentName") == "gain" and node.get("visible"))
    if float(gain["value"]) != 1:
        raise RuntimeError("Audio gain undo failed")
    step("show gain graph", "click", "--name", "Graph", "--class", "MotionTabs::Tab", "--exact")
    step("gain graph screenshot", "screenshot", "--file", session.artifact_dir / "soundtrack-gain.png")
    # The output picker shows in the Scope header when there is room (it is
    # also in the Audio menu).
    step("widen workspace", "resize-window", "--w", 1440, "--h", 800)
    step("select XY output", "select-option", "--name", "Audio output mode", "--text", "Beam X/Y")
    if os.environ.get("MOTION_TEST_XYRGB") == "1":
        step("enable five channel output", "select-option", "--name", "Audio output mode", "--text", "Beam XYRGB (5 ch)")
        output = find(snapshot(), lambda node: node.get("componentName") == "Audio output mode")
        if output is None or "XYRGB" not in str(output.get("value", "")):
            raise RuntimeError("Five-channel device configuration failed")
        step("five channel output screenshot", "screenshot", "--file", session.artifact_dir / "xyrgb-output.png")
    step("restore music monitor", "select-option", "--name", "Audio output mode", "--text", "Soundtrack")
    step("return to timeline", "click", "--name", "Timeline", "--class", "MotionTabs::Tab", "--exact")
    step("split soundtrack", "press", "command + k", "--class", "MotionTimelineView")
    wait_undo("Split clip")
    step("undo audio split", "click", "--name", "Undo", "--exact")
    step("play soundtrack", "click", "--name", "Play", "--exact")
    step("pause soundtrack", "click", "--name", "Pause", "--exact")
    print("Motion soundtrack workflow smoke passed", flush=True)
finally:
    session.stop_app()
