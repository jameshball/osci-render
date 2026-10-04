#!/usr/bin/env python3
"""Keyed cameras stay drivable: key a camera's rotation, look through it, key
the whole camera, then move the view at a later time and check that every
camera property is keyed at both times."""
import json
import struct
import subprocess
import time
import xml.etree.ElementTree as ET

from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession


session = BrowserSession(parse_args())
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("camera-drive")
session.keep_app = keep


def command(*args):
    return subprocess.check_output(session.cli(*[str(arg) for arg in args]), text=True)


def step(label, *args):
    if not session.run_step(label, session.cli(*[str(arg) for arg in args])):
        raise RuntimeError(label)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def find(predicate):
    return next((n for n in nodes(json.loads(command("snapshot", "--json", "--full"))) if predicate(n)), None)


def tool(name):
    found = find(lambda n: n.get("name") == name and n.get("role") == "button")
    if found is None:
        raise RuntimeError(name + " is missing")
    return found


def state(name, field, expected, timeout=5.0):
    """Waits for a button's enabled or checked state: the editor refreshes
    its camera tools on a timer."""
    deadline = time.monotonic() + timeout
    while True:
        value = bool(tool(name).get(field, False))
        if value == expected or time.monotonic() > deadline:
            return value
        time.sleep(0.1)


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Camera drive", duration="8", bpm="120", fps="30")
asset = ET.SubElement(composition, "asset", id="1", name="Drive triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Shape", kind="visual")
ET.SubElement(track, "clip", id="3", asset="1", name="Shape", start="0", duration="8", offset="0", rate="1",
              timeBase="seconds", contentBpm="120")
ET.SubElement(composition, "camera", id="9", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "camera-drive.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
camera_properties = ["position.x", "position.y", "position.z", "rotation.x", "rotation.y", "rotation.z", "fov"]


def saved_camera():
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    tree = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")
    return next(c for c in tree.iter("camera") if c.get("id") == "9")


def key_times(camera, name):
    prop = camera.find(f"property[@name='{name}']")
    return [] if prop is None else sorted(round(float(k.get("time")), 3) for k in prop.findall("key"))


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Camera drive", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    timeline = find(lambda n: n.get("class") == "MotionTimelineView")["bounds"]
    step("fit", "press", "F", "--class", "MotionTimelineView")
    pps = (timeline["w"] - 220 - 20) / 8.0
    x_of = lambda t: 220 + round(t * pps)
    # Between cuts the Cameras band shows the first camera; clicking selects it.
    step("select camera", "click", "--class", "MotionTimelineView", "--position", f"{x_of(4)},39")
    command("wait-for-locator", "--name", "Key rotation", "--exact")
    assert state("Key camera", "enabled", True), "Key camera is unavailable with a camera selected"
    # Keying rotation keys a level Z rotation: the camera stays drivable.
    step("key rotation", "click", "--name", "Key rotation", "--exact")
    session.wait_for_undo("Set keyframe")
    assert state("Look through camera", "enabled", True), "Keyed rotation blocked Look through camera"
    step("look through", "click", "--name", "Look through camera", "--exact")
    assert state("Look through camera", "checked", True), "Look through camera did not lock"
    step("key whole camera", "click", "--name", "Key camera", "--exact")
    session.wait_for_undo("Key camera")
    assert state("Key camera", "checked", True), "Key camera does not show the key at the playhead"
    camera = saved_camera()
    for name in camera_properties:
        assert key_times(camera, name) == [0.0], (name, key_times(camera, name))
    # A later time: moving the view keys the whole camera there.
    step("playhead to 2 s", "click", "--class", "MotionTimelineView", "--position", f"{x_of(2)},12")
    assert not state("Key camera", "checked", False), "Key camera shows a key where there is none"
    scene = find(lambda n: n.get("class") == "MotionCompositionView")["bounds"]
    step("orbit view", "wheel", scene["x"] + scene["w"] // 2, scene["y"] + scene["h"] // 2, "--dy", "0.2")
    session.wait_for_undo("Move camera")
    assert state("Look through camera", "checked", True), "Moving a keyed camera released Look through camera"
    step("driven camera", "screenshot", "--file", session.artifact_dir / "camera-drive.png")
    camera = saved_camera()
    for name in camera_properties:
        assert key_times(camera, name) == [0.0, 2.0], (name, key_times(camera, name))
    first = lambda name: float(camera.find(f"property[@name='{name}']").findall("key")[0].get("value"))
    second = lambda name: float(camera.find(f"property[@name='{name}']").findall("key")[1].get("value"))
    assert abs(first("rotation.x") - second("rotation.x")) > 0.5, "Orbiting did not change the keyed pitch"
    # Removing the key at the playhead.
    step("remove camera key", "click", "--name", "Key camera", "--exact")
    session.wait_for_undo("Remove camera key")
    camera = saved_camera()
    for name in camera_properties:
        assert key_times(camera, name) == [0.0], (name, key_times(camera, name))
    print("Motion camera drive passed", flush=True)
finally:
    session.stop_app()
