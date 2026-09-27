#!/usr/bin/env python3
"""Exercise Motion's selected-object path guide, key seeking and keyed gizmo editing."""
import json
import struct
import subprocess
import xml.etree.ElementTree as ET

from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession


def memory_block_encoding(data):
    """JUCE MemoryBlock's little-endian bit encoding, used by .osci-motion."""
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    padded = data + b"\0\0"
    encoded = []
    for bit in range(0, len(data) * 8, 6):
        byte, shift = divmod(bit, 8)
        value = padded[byte] | (padded[byte + 1] << 8)
        encoded.append(alphabet[(value >> shift) & 63])
    return str(len(data)) + "." + "".join(encoded)


def add_property(clip, name, base, keys=()):
    property_xml = ET.SubElement(clip, "property", name=name, base=str(base))
    for time, value in keys:
        ET.SubElement(property_xml, "key", time=str(time), value=str(value), interpolation="2", **{"in": "0", "out": "0"})


def create_fixture(destination):
    root = ET.Element("motion-project", schema="1")
    composition = ET.SubElement(root, "composition", name="Motion path study", duration="6", fps="24", bpm="120")
    triangle = b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n"
    ET.SubElement(composition, "asset", id="1", name="Path triangle.obj", extension=".obj").text = memory_block_encoding(triangle)
    track = ET.SubElement(composition, "track", id="2", name="Path track", kind="visual")
    clip = ET.SubElement(track, "clip", id="3", asset="1", name="Path object", start="0", duration="6", offset="0", rate="1", timeBase="seconds", contentBpm="120")
    add_property(clip, "position.x", "-0.6", ((0, -0.6), (2.123456, 0), (6, 0.6)))
    add_property(clip, "position.y", "-0.3", ((0, -0.3), (2.123456, 0.5), (6, -0.3)))
    add_property(clip, "position.z", 0)
    for name in ("rotation.x", "rotation.y", "rotation.z"):
        add_property(clip, name, 0)
    for name in ("scale.x", "scale.y", "scale.z"):
        add_property(clip, name, 0.15)
    for name in ("red", "green", "blue"):
        add_property(clip, name, 1)
    add_property(clip, "weight", 1)
    ET.SubElement(composition, "camera", id="4", name="Output camera")
    xml = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-path")
session.keep_app = keep
project = session.artifact_dir / "motion-path.osci-motion"
create_fixture(project)


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


def property_value(name):
    node = next(node for node in nodes(snapshot()) if node.get("componentId") == "motion." + name)
    return float(node.get("value", node.get("text", "nan")))


def saved_keys(axis):
    step("save path project", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    root = ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]])
    property_xml = root.find("./composition/track/clip[@name='Path object']/property[@name='position." + axis + "']")
    assert property_xml is not None
    return [(float(key.get("time")), float(key.get("value")), key.get("interpolation"), float(key.get("in")), float(key.get("out")))
            for key in property_xml.findall("key")]


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Motion path study", "--timeout-ms", 10000)
    step("mute isolated test output", "press", "command + shift + m", "--class", "MotionEditor")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("select path object clip", "click", "--class", "MotionTimelineView", "--position", "200,46")
    step("show selected motion path", "click", "--component-name", "Show motion path", "--exact")
    step("motion path screenshot", "screenshot", "--file", session.artifact_dir / "motion-path.png")
    composition = next(node for node in nodes(snapshot()) if node.get("class") == "MotionCompositionView")
    bounds = composition["bounds"]
    center_x = bounds["x"] + bounds["w"] / 2
    center_y = bounds["y"] + bounds["h"] / 2
    output_size = min(bounds["w"], bounds["h"]) - 48
    middle_x = round(center_x)
    middle_y = round(center_y - 0.5 * output_size / 2)
    step("seek middle path key", "click", "--class", "MotionCompositionView", "--position", f"{middle_x - bounds['x']},{middle_y - bounds['y']}")
    command("wait-for-value", "--component-name", "Timeline position", "--value", "2.123s", "--timeout-ms", 5000)
    before_x = property_value("position.x")
    before_x_keys = saved_keys("x")
    before_y_keys = saved_keys("y")
    assert before_x == 0
    expected_x = [(0.0, -0.6, "2", 0.0, 0.0), (2.123456, 0.0, "2", 0.0, 0.0), (6.0, 0.6, "2", 0.0, 0.0)]
    expected_y = [(0.0, -0.3, "2", 0.0, 0.0), (2.123456, 0.5, "2", 0.0, 0.0), (6.0, -0.3, "2", 0.0, 0.0)]
    assert before_x_keys == expected_x
    assert before_y_keys == expected_y
    # A current-time path key deliberately falls through hit testing to the centre move gizmo.
    step("drag current key through centre gizmo", "drag-xy", middle_x, middle_y, middle_x + 40, middle_y, "--steps", 12)
    moved_x = property_value("position.x")
    if not moved_x > before_x + 0.05:
        raise RuntimeError(f"Current keyed object did not move right: {before_x} -> {moved_x}")
    moved_x_keys = saved_keys("x")
    moved_y_keys = saved_keys("y")
    assert len(moved_x_keys) == len(moved_y_keys) == 3
    assert [key[0] for key in moved_x_keys] == [0.0, 2.123456, 6.0]
    assert [key[0] for key in moved_y_keys] == [0.0, 2.123456, 6.0]
    assert moved_x_keys[1][1] > 0.05
    assert [key[2:] for key in moved_x_keys] == [key[2:] for key in before_x_keys]
    assert moved_y_keys == before_y_keys
    step("undo keyed gizmo move", "click", "--name", "Undo", "--exact")
    assert property_value("position.x") == before_x
    assert saved_keys("x") == before_x_keys
    assert saved_keys("y") == before_y_keys
    step("reselect exact middle key for inspector", "click", "--class", "MotionCompositionView", "--position", f"{middle_x - bounds['x']},{middle_y - bounds['y']}")
    for axis, value in (("x", "0.2"), ("y", "0.4")):
        field = next(node for node in nodes(snapshot()) if node.get("componentId") == "motion.position." + axis)
        step("edit selected key in inspector " + axis, "fill", field["ref"], value)
        keys = saved_keys(axis)
        assert len(keys) == 3 and [key[0] for key in keys] == [0.0, 2.123456, 6.0]
        assert abs(keys[1][1] - float(value)) < 1e-12
    step("undo inspector Y", "click", "--name", "Undo", "--exact")
    step("undo inspector X", "click", "--name", "Undo", "--exact")
    assert saved_keys("x") == before_x_keys
    assert saved_keys("y") == before_y_keys
    endpoint_x = round(center_x + 0.6 * output_size / 2)
    endpoint_y = round(center_y + 0.3 * output_size / 2)
    step("seek exact endpoint path key", "click", "--class", "MotionCompositionView", "--position", f"{endpoint_x - bounds['x']},{endpoint_y - bounds['y']}")
    command("wait-for-value", "--component-name", "Timeline position", "--value", "6.000s", "--timeout-ms", 5000)
    endpoint_before = saved_keys("x")
    step("drag endpoint key through centre gizmo", "drag-xy", endpoint_x, endpoint_y, endpoint_x + 40, endpoint_y, "--steps", 12)
    endpoint_moved = saved_keys("x")
    assert len(endpoint_moved) == 3
    assert [key[0] for key in endpoint_moved] == [0.0, 2.123456, 6.0]
    assert endpoint_moved[-1][1] > endpoint_before[-1][1] + 0.05
    step("undo endpoint gizmo move", "click", "--name", "Undo", "--exact")
    assert saved_keys("x") == endpoint_before
    step("toggle path shortcut off", "press", "P", "--class", "MotionCompositionView")
    step("compact workspace", "resize-window", "--w", 1100, "--h", 700)
    step("compact path off screenshot", "screenshot", "--file", session.artifact_dir / "motion-path-compact-off.png")
    step("toggle path shortcut on", "press", "P", "--class", "MotionCompositionView")
    step("compact path on screenshot", "screenshot", "--file", session.artifact_dir / "motion-path-compact-on.png")
    print("Motion path seek, keyed gizmo edit, undo and compact path views passed", flush=True)
finally:
    session.stop_app()
