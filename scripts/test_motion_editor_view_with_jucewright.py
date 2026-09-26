#!/usr/bin/env python3
"""Check independent editor framing, perspective dragging and drag cancellation."""
import json
import subprocess
from PIL import Image
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-editor-view")
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


def property_value(name):
    node = next(n for n in nodes(snapshot()) if n.get("componentId") == "motion." + name)
    return float(node.get("value", node.get("text", "nan")))


def preview_point():
    tree = snapshot()
    bounds = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionCompositionView")
    path = session.artifact_dir / "preview-picking.png"
    command("screenshot", "--file", path)
    image = Image.open(path).convert("RGB")
    y = bounds["y"] + bounds["h"] // 2
    candidates = []
    for x in range(bounds["x"] + 8, bounds["x"] + bounds["w"] - 8):
        r, g, b = image.getpixel((x, y))
        if g > 100 and g > r + 25 and g > b + 15:
            candidates.append(x)
    if not candidates:
        raise RuntimeError("No cube edge found in the composition preview")
    return candidates[0], y, bounds


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("import cube", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    initial = [property_value("position." + axis) for axis in "xyz"]
    x, y, bounds = preview_point()
    step("move object in preview", "drag-xy", x, y, x + 40, y, "--steps", 12)
    moved = property_value("position.x")
    if not moved > initial[0] + 0.05:
        raise RuntimeError(f"Object did not move right: {initial[0]} -> {moved}")
    step("undo preview move", "click", "--name", "Undo", "--exact")
    assert [property_value("position." + axis) for axis in "xyz"] == initial
    step("frame selection", "click", "--name", "Fit", "--class", "juce::TextButton", "--exact")
    assert [property_value("position." + axis) for axis in "xyz"] == initial
    step("reset editing view", "press", "0", "--class", "MotionCompositionView")
    step("key position", "click", "--name", "Key position.x", "--exact")
    x, y, bounds = preview_point()
    step("begin reversible drag", "mouse-down", x, y)
    step("preview reversible drag", "mouse-move", x + 35, y)
    assert property_value("position.x") > initial[0] + 0.05
    step("cancel drag", "press", "Escape", "--class", "MotionCompositionView")
    step("release cancelled drag", "mouse-up", x + 35, y)
    assert [property_value("position." + axis) for axis in "xyz"] == initial
    step("begin round trip drag", "mouse-down", x, y)
    step("move round trip drag", "mouse-move", x + 35, y)
    step("return drag to start", "mouse-move", x, y)
    step("finish unchanged drag", "mouse-up", x, y)
    assert [property_value("position." + axis) for axis in "xyz"] == initial
    step("zoom editor view", "wheel", bounds["x"] + bounds["w"] // 2, y, "--dy", -0.3)
    assert [property_value("position." + axis) for axis in "xyz"] == initial
    step("independent view screenshot", "screenshot", "--file", session.artifact_dir / "independent-view.png")
    step("reset view after zoom", "press", "0", "--class", "MotionCompositionView")
    step("final workspace screenshot", "screenshot", "--file", session.artifact_dir / "workspace.png")
    print("Motion editor view workflow passed", flush=True)
finally:
    session.stop_app()
