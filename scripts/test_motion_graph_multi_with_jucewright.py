#!/usr/bin/env python3
"""Graph editor: cross-curve box select, group drag, magnets, key scaling, delete, key fields and key bar."""
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
session.launch_app("motion-graph-multi")
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


def find(predicate):
    return next((n for n in nodes(json.loads(command("snapshot", "--json", "--full"))) if predicate(n)), None)


def wait_undo(text):
    session.wait_for_undo(text)


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


# One 4 s clip whose position axes are all keyed (linear). The second Y key sits
# off the 30 fps grid so a magnet snap is distinguishable from a grid snap.
FPS = 30
AUTHORED = {
    "position.x": [(1, -1), (2, 0), (3, 1)],
    "position.y": [(1, 0.5), (3.21, -0.5)],
    "position.z": [(2.4, 0.6)],
}
root = ET.Element("motion-project", schema="1")
composition = ET.SubElement(root, "composition", name="Graph study", duration="8", bpm="120", fps=str(FPS))
asset = ET.SubElement(composition, "asset", id="1", name="Graph triangle.obj", extension=".obj")
asset.text = encoded(b"v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n")
track = ET.SubElement(composition, "track", id="2", name="Row", kind="visual")
clip = ET.SubElement(track, "clip", id="3", asset="1", name="Clip", start="0", duration="4", offset="0", rate="1", timeBase="seconds", contentBpm="120")
for group, base in (("position", 0), ("rotation", 0), ("scale", 1)):
    for axis in "xyz":
        prop = ET.SubElement(clip, "property", name=f"{group}.{axis}", base=str(base))
        for t, v in AUTHORED.get(f"{group}.{axis}", []):
            ET.SubElement(prop, "key", time=str(t), value=str(v), interpolation="1", **{"in": "0", "out": "0"})
for name, base in (("red", 1), ("green", 1), ("blue", 1), ("weight", 1)):
    ET.SubElement(clip, "property", name=name, base=str(base))
ET.SubElement(composition, "camera", id="9", name="Camera")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "graph-multi.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")


def saved():
    step("save", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


def keys(tree):
    clip = next(c for c in tree.iter("clip") if c.get("id") == "3")
    result = {}
    for axis in "xyz":
        prop = clip.find(f"property[@name='position.{axis}']")
        result[f"position.{axis}"] = [(float(k.get("time")), float(k.get("value"))) for k in prop.findall("key")]
    return result


def expect(actual, expected, label):
    for name, want in expected.items():
        got = actual[name]
        ok = len(got) == len(want) and all(abs(a[0] - b[0]) < 1e-4 and abs(a[1] - b[1]) < 1e-4 for a, b in zip(got, want))
        if not ok:
            raise RuntimeError(f"{label}: {name} is {got}, expected {want}")


# Mirrors MotionCurveEditor::fit for linear keys inside the clip: the view frames
# the clip [0, 4] (plus key times) padded 4 %, and every key value with a 15 % margin.
def view(model):
    times = [t for curve in model.values() for t, _ in curve] + [0.0, 4.0]
    first, last = min(times), max(times)
    pad = (last - first) * 0.04
    values = [v for curve in model.values() for _, v in curve]
    low, high = min(values), max(values)
    if high - low < 1:
        middle = (low + high) / 2
        low, high = middle - 0.5, middle + 0.5
    margin = (high - low) * 0.15
    return first - pad, last + pad, low - margin, high + margin


def mapper(model):
    start, end, low, high = view(model)
    plot_x, plot_y, plot_w, plot_h = 56, 32, max(1, graph["w"] - 66), max(1, graph["h"] - 40)
    x_of = lambda t: plot_x + (t - start) / (end - start) * plot_w
    y_of = lambda v: plot_y + plot_h - (v - low) / (high - low) * plot_h
    return x_of, y_of, plot_w / (end - start)


def click(label, t, v, model, *extra):
    x_of, y_of, _ = mapper(model)
    step(label, "click", "--class", "MotionCurveEditor", "--position", f"{round(x_of(t))},{round(y_of(v))}", *extra)


def drag(label, start, end):
    step(label, "drag-xy", graph["x"] + round(start[0]), graph["y"] + round(start[1]), graph["x"] + round(end[0]), graph["y"] + round(end[1]), "--steps", 12)


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Graph study", "--timeout-ms", 60000)
    step("workspace", "resize-window", "--w", 1440, "--h", 900)
    timeline = find(lambda n: n.get("class") == "MotionTimelineView")["bounds"]
    step("fit timeline", "press", "F", "--class", "MotionTimelineView")
    pps = (timeline["w"] - 220 - 20) / 8.0
    step("select clip", "click", "--class", "MotionTimelineView", "--position", f"{220 + round(0.5 * pps)},64")
    step("open graph", "click", "--name", "Graph", "--class", "osci::TabBar::Tab", "--exact")
    graph = find(lambda n: n.get("class") == "MotionCurveEditor")["bounds"]
    model = {name: list(curve) for name, curve in AUTHORED.items()}
    click("focus graph", 0.5, 0.0, model)
    step("frame all", "press", "F", "--class", "MotionCurveEditor")
    step("graph", "screenshot", "--file", session.artifact_dir / "graph-initial.png")

    # 1. Box select across curves: X and Y keys at 1 s, not Z.
    x_of, y_of, graph_pps = mapper(model)
    drag("box select", (x_of(0.8), y_of(-1.25)), (x_of(1.2), y_of(0.8)))
    # 2. Dragging the X key moves the Y key too: one undo step on both curves.
    drag("group drag", (x_of(1), y_of(-1)), (x_of(1.5), y_of(-1)))
    wait_undo("Move animation keys")
    model["position.x"] = [(1.5, -1), (2, 0), (3, 1)]
    model["position.y"] = [(1.5, 0.5), (3.21, -0.5)]
    expect(keys(saved()), model, "group drag")
    step("undo group drag", "click", "--name", "Undo", "--exact")
    expect(keys(saved()), {name: list(curve) for name, curve in AUTHORED.items()}, "undo group drag")
    step("redo group drag", "click", "--name", "Redo", "--exact")
    expect(keys(saved()), model, "redo group drag")
    step("after group drag", "screenshot", "--file", session.artifact_dir / "graph-group-drag.png")

    # 3. Magnet: the Z key lands exactly on the off-grid Y key at 3.21 s.
    x_of, y_of, graph_pps = mapper(model)
    drag("magnet drag", (x_of(2.4), y_of(0.6)), (x_of(3.21) + 4, y_of(0.6)))
    wait_undo("Move animation key")
    model["position.z"] = [(3.21, 0.6)]
    expect(keys(saved()), model, "magnet drag")

    # 4. Scale: select five keys on X and Y, then drag the box's right edge by 0.5 s.
    x_of, y_of, graph_pps = mapper(model)
    click("select x 1.5", 1.5, -1, model)
    for t, v in ((2, 0), (3, 1), (1.5, 0.5), (3.21, -0.5)):
        click(f"add key {t},{v}", t, v, model, "--modifiers", "shift")
    step("scale selection", "screenshot", "--file", session.artifact_dir / "graph-scale-selection.png")
    handle = (x_of(3.21) + 12, (y_of(1) + y_of(-1)) / 2)
    drag("scale right edge", handle, (handle[0] + 0.5 * graph_pps, handle[1]))
    wait_undo("Scale animation keys")
    edge = round(3.71 * FPS) / FPS
    scale = lambda t: 1.5 + (t - 1.5) * (edge - 1.5) / (3.21 - 1.5)
    model["position.x"] = [(1.5, -1), (scale(2), 0), (scale(3), 1)]
    model["position.y"] = [(1.5, 0.5), (edge, -0.5)]
    expect(keys(saved()), model, "scale")
    step("after scale", "screenshot", "--file", session.artifact_dir / "graph-scaled.png")

    # 5. Box select X and Y keys at 1.5 s and delete both in one undo step.
    x_of, y_of, graph_pps = mapper(model)
    drag("box select for delete", (x_of(1.3), y_of(-1.25)), (x_of(1.7), y_of(0.8)))
    step("delete keys", "press", "Delete", "--class", "MotionCurveEditor")
    wait_undo("Delete animation keys")
    model["position.x"] = model["position.x"][1:]
    model["position.y"] = model["position.y"][1:]
    expect(keys(saved()), model, "delete")
    step("undo delete", "click", "--name", "Undo", "--exact")
    restored = dict(model)
    restored["position.x"] = [(1.5, -1)] + model["position.x"]
    restored["position.y"] = [(1.5, 0.5)] + model["position.y"]
    expect(keys(saved()), restored, "undo delete")

    # 6. The Key panel types the one selected key's value; the key bar sets
    # its interpolation.
    click("select one key", 1.5, -1, restored)
    step("type key value", "set-value", "--component-name", "Key value", "--exact", "-0.75")
    wait_undo("Change animation key")
    restored["position.x"] = [(1.5, -0.75)] + restored["position.x"][1:]
    expect(keys(saved()), restored, "typed key value")
    step("hold interpolation", "click", "--name", "Hold interpolation", "--exact")
    wait_undo("Change key interpolation")
    clip = next(c for c in saved().iter("clip") if c.get("id") == "3")
    first = clip.find("property[@name='position.x']").findall("key")[0]
    if first.get("interpolation") != "0":
        raise RuntimeError(f"key bar: interpolation is {first.get('interpolation')}, expected hold (0)")
    # Alt+Right nudges the selected key one frame; Escape then deselects.
    step("nudge key", "press", "alt + right", "--class", "MotionCurveEditor")
    wait_undo("Nudge animation key")
    restored["position.x"] = [(1.5 + 1 / FPS, -0.75)] + restored["position.x"][1:]
    expect(keys(saved()), restored, "nudge")
    step("deselect", "press", "Escape", "--class", "MotionCurveEditor")
    if find(lambda n: n.get("name") == "Key interpolation" and n.get("visible")):
        raise RuntimeError("Escape left the key bar showing")

    # 7. In bars and beats the Key panel reads and types time like the ruler.
    nudged = 1.5 + 1 / FPS
    click("select nudged key", nudged, -0.75, restored)
    step("timing menu", "click", "--name", "Timing", "--class", "juce::MenuBarComponent::AccessibleItemComponent", "--exact")
    step("bars and beats", "click", "--name", "Bars / beats", "--role", "menuItem", "--exact")
    shown = find(lambda n: n.get("name") == "Key time")
    # 1.5333 s at 120 BPM is 3.0667 beats: bar 1, beat 4, tick 64.
    if shown is None or shown.get("value") != "1.4.064":
        raise RuntimeError(f"Key time in bars and beats: {shown and shown.get('value')}")
    step("type key time in beats", "set-value", "--component-name", "Key time", "--exact", "1.3.480")
    wait_undo("Move animation key")
    restored["position.x"] = [(1.25, -0.75)] + restored["position.x"][1:]
    expect(keys(saved()), restored, "key time typed in bars and beats")
    step("final", "screenshot", "--file", session.artifact_dir / "graph-final.png")
    print("Graph cross-curve selection, group drag, magnets, scaling, delete, key fields (in seconds and bars), key bar and nudging passed.", flush=True)
finally:
    session.stop_app()
