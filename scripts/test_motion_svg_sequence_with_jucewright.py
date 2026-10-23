#!/usr/bin/env python3
"""Several SVGs dropped together: the sheet offers them as one animation (a frame each, in name order) or as separate sources."""
import json
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
session.launch_app("motion-svg-sequence")
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


import math
import time


def track_names():
    return sorted(node.get("value") for node in nodes(snapshot()) if str(node.get("componentName", "")).startswith("Track name "))


# A star turning and a ball crossing, on one page, numbered as an export names them.
frames = []
for index in range(24):
    turn = index / 24 * 2 * math.pi
    points = []
    for corner in range(10):
        radius = 60 if corner % 2 == 0 else 26
        angle = turn + corner * math.pi / 5
        points.append(f"{100 + radius * math.sin(angle):.2f},{100 - radius * math.cos(angle):.2f}")
    ball = 20 + index * 160 / 23
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200">'
           f'<polygon points="{" ".join(points)}" fill="none" stroke="black"/>'
           f'<circle cx="{ball:.2f}" cy="180" r="10" fill="none" stroke="black"/></svg>')
    path = session.artifact_dir / f"spin_{index + 1:03d}.svg"
    path.write_text(svg)
    frames.append(path)

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("drop the frames", "drop-files", *sum([["--file", frame] for frame in reversed(frames)], []), "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Add SVGs", "--exact")
    time.sleep(2.5)
    step("animation screenshot", "screenshot", "--file", session.artifact_dir / "animation.png", "--scale", "2")
    time.sleep(.35)
    step("animation later screenshot", "screenshot", "--file", session.artifact_dir / "animation-later.png", "--scale", "2")
    step("separate", "set-value", "--name", "Import as", "--role", "comboBox", "--exact", "Separate")
    time.sleep(.3)
    step("separate screenshot", "screenshot", "--file", session.artifact_dir / "separate.png", "--scale", "2")
    step("animation", "set-value", "--name", "Import as", "--role", "comboBox", "--exact", "Animation")
    step("add", "click", "--name", "Add SVGs", "--exact")
    session.wait_for_undo("Import source", 60)
    if track_names() != ["spin"]:
        raise RuntimeError("The sequence should be one source named for its frames: " + str(track_names()))
    time.sleep(.5)
    step("imported screenshot", "screenshot", "--file", session.artifact_dir / "imported.png", "--scale", "2")
    print("Motion SVG sequence passed", flush=True)
finally:
    session.stop_app()
