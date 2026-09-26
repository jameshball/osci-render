#!/usr/bin/env python3
"""Author the revised DAH music-video study through the actual Motion interface.

The initial project is empty. Every source, clip, property and key is created
through user-facing controls. This is an arrangement study requiring visual and export review.
Run create_motion_dah_assets.py with /private/tmp/dah-motion-assets as its
destination first; add the supplied soundtrack as a 48 kHz WAV in that folder.
"""
import json
import struct
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("dah-authoring")
session.keep_app = keep
assets = Path("/private/tmp/dah-motion-assets")


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def find(value, name):
    if isinstance(value, dict):
        if value.get("componentName") == name or value.get("class") == name:
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


def edit(name, value):
    target = find(json.loads(command("snapshot", "--json", "--full")), name)
    assert target is not None, name
    step("open " + name, "click", target["ref"], "--click-count", 2)
    target = find(json.loads(command("snapshot", "--json", "--full")), name)
    editor = find(target, "juce::TextEditor")
    assert editor is not None, "Editor missing: " + name
    step("set " + name, "fill", editor["ref"], str(value))
    step("commit " + name, "press", "Return")


def tab(name):
    step("inspect " + name, "click", "--name", name, "--class", "osci::TabBar::Tab", "--exact")


def seek(seconds):
    edit("Timeline position", str(seconds) + "s")


def import_source(name, start, duration, properties, animation=None):
    seek(start)
    step("import " + name, "drop-files", "--file", assets / name, "--class", "MotionEditor", "--exact")
    if name.endswith(".lua"):
        command("wait-for-locator", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact")
        step("set ribbon bake length", "fill", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact", "3.2")
        step("bake ribbon", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    elif name.endswith(".gif"):
        command("wait-for-locator", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
        step("trace animated raster", "click", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", name, "--class", "juce::Label", "--exact")
    tab("Clip")
    edit("Clip duration", duration)
    if properties or animation:
        tab("Object")
        for property_, value in properties.items():
            edit(property_, value)
        for property_, keys in (animation or {}).items():
            seek(start)
            edit(property_, keys[0][1])
            step("key " + property_, "click", "--name", "Key " + property_, "--exact")
            for time, value in keys[1:]:
                seek(time)
                edit(property_, value)
    step("save imported layer", "press", "command + s", "--class", "MotionEditor")


def repeat_selected(start, duration):
    step("repeat selected motif", "press", "command + d", "--class", "MotionTimelineView")
    tab("Clip")
    edit("Clip start", start)
    edit("Clip duration", duration)


def camera(name, start, properties, animation=None):
    seek(start)
    tab("Camera")
    step("add output camera", "click", "--name", "Add", "--class", "juce::TextButton", "--exact")
    edit("Camera name", name)
    for key, value in properties.items():
        edit("Camera " + key, value)
    for key, keys in (animation or {}).items():
        seek(start)
        edit("Camera " + key, keys[0][1])
        step("key camera " + key, "click", "--name", "Key camera " + key, "--exact")
        for moment, value in keys[1:]:
            seek(moment)
            edit("Camera " + key, value)
    seek(start)
    step("cut to " + name, "click", "--name", "Cut here", "--exact")


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="DAH — kinetic study", duration="185.6", bpm="150", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "DAH.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def motif(name, intervals, size, color, position=(0, 0), axis="rotation.y", period=3.2):
    first, length = intervals[0]
    properties = dict(zip(("red", "green", "blue"), color))
    properties.update({"scale.x": size, "scale.y": size, "scale.z": size,
                       "position.x": position[0], "position.y": position[1]})
    # Alternating orientation every half phrase gives continuous, faster motion
    # without depending on unbounded rotation controls.
    keys = [(round(first + i * period / 2, 6), -65 if i % 2 == 0 else 65)
            for i in range(int(length * 2 / period) + 1)]
    import_source(name, first, length, properties, {axis: keys})
    for start, duration in intervals[1:]:
        repeat_selected(start, duration)
    print("Finished motif:", name, flush=True)


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    step("size authoring workspace", "resize-window", "--w", 1600, "--h", 1000)
    motif("02 Faceted glider.obj", [(0, 12.8), (25.6, 12.8), (51.2, 12.8), (128, 12.8), (166.4, 12.8)], .58, (1,.45,.12), axis="rotation.y")
    motif("01 Helix ladder.obj", [(6.4, 12.8), (44.8, 12.8), (64, 12.8), (115.2, 12.8), (153.6, 12.8)], .65, (.2,.8,1), axis="rotation.z")
    motif("03 Counterweight mobile.obj", [(12.8, 12.8), (38.4, 12.8), (57.6, 12.8), (121.6, 12.8), (160, 12.8)], .42, (.7,.35,1), (-.45,.15), axis="rotation.x")
    motif("04 Folded signal wall.obj", [(19.2, 12.8), (51.2, 12.8), (108.8, 12.8), (140.8, 12.8)], .65, (.2,1,.6), (0,-.4), axis="rotation.y", period=6.4)
    motif("05 Impossible steps.obj", [(32, 12.8), (64, 12.8), (96, 12.8), (147.2, 12.8)], .55, (1,.3,.5), (.25,-.3), axis="rotation.z")
    motif("06 Mechanical wings.json", [(9.6, 12.8), (48, 12.8), (76.8, 12.8), (134.4, 12.8), (166.4, 12.8)], .5, (1,1,1), (.25,.25), axis="rotation.z", period=6.4)
    motif("07 Travelling signal loom.json", [(0, 12.8), (38.4, 12.8), (83.2, 12.8), (108.8, 12.8), (153.6, 12.8)], .65, (1,1,1), (0,-.45), axis="rotation.x", period=6.4)
    motif("08 Geometric night walker.gif", [(25.6, 12.8), (70.4, 12.8), (89.6, 12.8), (128, 12.8)], .4, (1,1,1), (-.45,-.3), axis="rotation.y", period=6.4)
    motif("09 Origami flight.gif", [(16, 12.8), (57.6, 12.8), (102.4, 12.8), (147.2, 12.8)], .35, (1,1,1), (.5,.35), axis="rotation.z", period=6.4)
    motif("Electric braid.lua", [(44.8, 12.8), (64, 12.8), (96, 12.8), (140.8, 12.8), (166.4, 12.8)], .7, (1,1,1), axis="rotation.y", period=6.4)
    motif("Folded wave.lua", [(32, 12.8), (76.8, 12.8), (115.2, 12.8), (153.6, 12.8)], .7, (1,1,1), axis="rotation.x", period=6.4)
    for name, starts, y in [("DAH.txt", [0,12.8,51.2,128,176], -.65),
                            ("DO.txt", [6.4,32,64,96,153.6], .6),
                            ("DAH DAH.txt", [19.2,57.6,108.8,140.8,179.2], -.6)]:
        import_source(name, starts[0], 1.6, {"scale.x":.35,"scale.y":.35,"scale.z":.35,"position.y":y,"red":1,"green":.85,"blue":.4})
        for start in starts[1:]: repeat_selected(start,1.6)
    # Companion gliders keep depth and motion active between foreground changes.
    motif("02 Faceted glider.obj", [(6.4,12.8),(32,12.8),(70.4,12.8),(102.4,12.8),(140.8,12.8)], .2, (.4,.8,1), (.65,-.2), axis="rotation.z")
    import_source("dahhhhh do da dahh!.wav", 0, 185.6, {})
    tab("Timeline")
    step("fit composition", "press", "F", "--class", "MotionTimelineView")
    for moment in (3.2,16,35.2,54.4,67.2,80,99.2,118.4,137.6,156.8,172.8,180):
        seek(moment)
        command("wait", "--ms", 800)
        step("review " + str(moment), "screenshot", "--file", session.artifact_dir / f"review-{moment:06.1f}.png")
    step("save revised study", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    saved = ET.fromstring(data[8:8 + struct.unpack("<I",data[4:8])[0]])
    assert len(saved.findall("./composition/track")) == 16
    assert len(saved.findall("./composition/track/clip")) >= 65
    print("DAH arrangement authored and saved; visual/export review remains", flush=True)
finally:
    session.stop_app()
