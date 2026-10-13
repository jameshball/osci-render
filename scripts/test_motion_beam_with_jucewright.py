#!/usr/bin/env python3
"""Drag a clip's beam share in the timeline: its Beam changes, and Cmd-click keys it."""
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
session.launch_app("motion-beam")
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


def named(name):
    for node in nodes(snapshot()):
        if name in (node.get("componentName"), node.get("name"), node.get("value")):
            return node
    raise RuntimeError("Missing " + name)


def beam_value():
    return float(str(named("beam").get("value", "")).rstrip("×x ").strip() or "nan")


shot_index = [0]


def lit_edge(x, y):
    """Whether the hovered share edge (the accent line) is lit around (x, y)."""
    shot_index[0] += 1
    path = session.artifact_dir / f"edge-{shot_index[0]}.png"
    command("screenshot", "--file", path)
    image = Image.open(path).convert("RGB")
    scale = image.width / window["w"]
    count = 0
    for px in range(int((x - 30) * scale), int((x + 30) * scale)):
        for py in range(int((y - 3) * scale), int((y + 3) * scale)):
            r, g, b = image.getpixel((px, py))
            if g > 170 and r < 140 and b < 140 and g - r > 80:
                count += 1
    return count > 20 * scale


def find_edge(x, top, bottom):
    for y in range(bottom, top, -1):
        command("hover", x, y)
        if lit_edge(x, y):
            return y
    raise RuntimeError("No beam edge found on the clip")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    for source in ["Resources/models/cube.obj", "Resources/svg/cat.svg"]:
        step("import " + source, "drop-files", "--file", session.root_dir / source, "--class", "MotionEditor", "--exact")
        session.wait_for_undo("Import source")
    tree = snapshot()
    window = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionEditor")
    header = next(n["bounds"] for n in nodes(tree) if str(n.get("componentName", "")).startswith("Track name ") and n.get("value") == "cube.obj")
    headers = [n["bounds"] for n in nodes(tree) if n.get("name") == "Track header"]
    names_right = max(b["x"] + b["w"] for b in headers)
    x = names_right + 160
    middle = header["y"] + header["h"] // 2
    edge = find_edge(x, middle - 16, middle + 16)
    step("beam edge screenshot", "screenshot", "--file", session.artifact_dir / "edge.png", "--scale", "2")
    before = beam_value() if any(n.get("componentName") == "beam" for n in nodes(snapshot())) else 1.0
    step("press the edge", "mouse-down", x, edge)
    step("raise it", "mouse-move", x, edge - 6)
    step("raise it more", "mouse-move", x, edge - 10)
    step("dragging screenshot", "screenshot", "--file", session.artifact_dir / "dragging.png", "--scale", "2")
    step("let go", "mouse-up", x, edge - 10)
    session.wait_for_undo("Change Beam")
    raised = beam_value()
    if not raised > before:
        raise RuntimeError(f"Raising the edge should raise the clip's Beam ({before} -> {raised})")
    # Cmd-click on the edge keys the Beam there. Nothing selected, so only
    # the hovered edge is lit in the accent colour.
    step("clear the selection", "click-xy", x, middle + 90)
    edge = find_edge(x + 80, middle - 16, middle + 16)
    step("key the beam", "click-xy", x + 80, edge, "--modifiers", "cmd")
    step("keyed screenshot", "screenshot", "--file", session.artifact_dir / "keyed.png", "--scale", "2")
    # Keyed, Properties offers its key navigation.
    command("wait-for-locator", "--component-name", "Next beam key", "--exact", "--timeout-ms", 5000)
    step("undo the key", "click", "--name", "Undo", "--exact")
    step("undo the drag", "click", "--name", "Undo", "--exact")
    session.wait_for_undo("Import source")
    if abs(beam_value() - before) > 1e-6:
        raise RuntimeError("Undo should restore the Beam")
    print("Motion beam workflow passed", flush=True)
finally:
    session.stop_app()
