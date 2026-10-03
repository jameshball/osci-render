#!/usr/bin/env python3
"""Exercise contextual modulation, base-value keying, tempo and undo."""
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
session.launch_app("motion-modulation")
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
    command("wait-for-locator", "--name", "Undo " + text, "--role", "label", "--exact")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1100, "--h", 800)
    step("import object", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    step("open graph", "click", "--name", "Graph", "--class", "MotionTabs::Tab", "--exact")
    step("enable modulation", "click", "--name", "Enable modulation", "--exact")
    wait_undo("Change modulation")
    step("set amount", "set-value", "--name", "Modulation amount", "--role", "slider", "0.5")
    step("set phase", "set-value", "--name", "Modulation phase", "--role", "slider", "90")
    step("key authored position", "click", "--name", "Key position", "--exact")
    wait_undo("Set keyframe")
    position = find(snapshot(), lambda node: node.get("componentName") == "position.x")
    if abs(float(position["value"])) > 0.00001:
        raise RuntimeError("Keying baked the modulated value into the authored position")
    step("sine modulation screenshot", "screenshot", "--file", session.artifact_dir / "sine-modulation.png")
    step("tempo sync", "select-option", "--name", "Modulation clock", "--text", "Beats")
    step("beats per cycle", "set-value", "--name", "Modulation rate", "--role", "slider", "4")
    tempo = find(snapshot(), lambda node: node.get("componentName") == "Project tempo")
    step("edit tempo", "fill", tempo["ref"], "90")
    step("commit tempo", "press", "Return")
    wait_undo("Change tempo")
    step("choose random", "select-option", "--name", "Modulation waveform", "--text", "Smooth random")
    seed_before = find(snapshot(), lambda node: node.get("componentName") == "Modulation seed")["value"]
    step("new random seed", "click", "--name", "New seed", "--exact")
    seed_after = find(snapshot(), lambda node: node.get("componentName") == "Modulation seed")["value"]
    if seed_before == seed_after:
        raise RuntimeError("New seed did not change the deterministic seed")
    step("undo seed", "click", "--name", "Undo", "--exact")
    restored = find(snapshot(), lambda node: node.get("componentName") == "Modulation seed")["value"]
    if restored != seed_before:
        raise RuntimeError("Undo did not restore the seed")
    step("redo seed", "click", "--name", "Redo", "--exact")
    step("random modulation screenshot", "screenshot", "--file", session.artifact_dir / "random-modulation.png")
    amount_node = find(snapshot(), lambda node: node.get("componentName") == "Modulation amount" and node.get("role") == "slider")
    before_drag = float(amount_node["value"])
    bounds = amount_node["bounds"]
    x, y = bounds["x"] + (bounds["w"] - 62) // 2, bounds["y"] + bounds["h"] // 2
    step("drag modulation amount", "drag-xy", x, y, x + 20, y, "--steps", 10)
    changed = find(snapshot(), lambda node: node.get("componentName") == "Modulation amount" and node.get("role") == "slider")
    if float(changed["value"]) == before_drag:
        raise RuntimeError("Modulation drag did not preview a new amount")
    step("undo modulation drag", "click", "--name", "Undo", "--exact")
    restored = find(snapshot(), lambda node: node.get("componentName") == "Modulation amount" and node.get("role") == "slider")
    if abs(float(restored["value"]) - before_drag) > 0.001:
        raise RuntimeError("One undo did not restore the entire modulation gesture")
    step("start cancellable modulation drag", "mouse-down", x, y)
    step("move modulation amount", "mouse-move", x + 20, y)
    step("cancel modulation drag", "press", "Escape", "--name", "Modulation amount", "--role", "slider", "--exact")
    step("finish cancelled modulation drag", "mouse-up", x + 20, y)
    restored = find(snapshot(), lambda node: node.get("componentName") == "Modulation amount" and node.get("role") == "slider")
    if abs(float(restored["value"]) - before_drag) > 0.001:
        raise RuntimeError("Escape did not restore the modulation gesture")
    step("keyboard increment modulation", "press", "Right", "--name", "Modulation amount", "--role", "slider", "--exact")
    incremented = find(snapshot(), lambda node: node.get("componentName") == "Modulation amount" and node.get("role") == "slider")
    if float(incremented["value"]) <= before_drag:
        raise RuntimeError("Keyboard did not increment the modulation amount")
    step("undo keyboard increment", "click", "--name", "Undo", "--exact")
    step("multiplicative modulation", "select-option", "--name", "Modulation mode", "--text", "Multiply")
    wait_undo("Change modulation")
    step("switch property", "click", "--name", "Curve Position Y", "--exact")
    step("return property", "click", "--name", "Curve Position X", "--exact")
    step("compact workspace", "resize-window", "--w", 1100, "--h", 700)
    step("collapse graph height", "drag", "--name", "Resize timeline", "--class", "osci::PanelDivider", "--exact", "--position", "20,3", "--dx", 0, "--dy", 200)
    tree = snapshot()
    panel = find(tree, lambda node: node.get("class") == "MotionModulationPanel")["bounds"]
    seed = find(tree, lambda node: node.get("name") == "New seed")["bounds"]
    if seed["y"] + seed["h"] > panel["y"] + panel["h"]:
        raise RuntimeError("Random controls are clipped at minimum graph height")
    step("compact modulation screenshot", "screenshot", "--file", session.artifact_dir / "compact-modulation.png")
    print("Motion modulation workflow smoke passed", flush=True)
finally:
    session.stop_app()
