#!/usr/bin/env python3
"""Exercise scoped Motion effects through real dragging and editable controls."""
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
session.launch_app("motion-effects")
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


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", session.window_width or 1100, "--h", session.window_height or 800)
    step("import object", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    library = find(snapshot(), lambda node: node.get("class") == "osci::TabBar" and node.get("name") == "Library tabs")
    effectTab = find(library, lambda node: node.get("class") == "osci::TabBar::Tab" and node.get("name") == "Effects")
    step("open effects library", "click", effectTab["ref"])

    def tile(name):
        return find(snapshot(), lambda node: str(node.get("class", "")).endswith("Tile") and node.get("componentName") == name)

    # Dragging an effect onto a clip adds it there and shows it in Properties.
    tree = snapshot()
    source = tile("Swirl")["bounds"]
    target = find(tree, lambda node: node.get("class") == "MotionTimelineView")["bounds"]
    step("drag effect onto clip", "drag-xy", source["x"] + source["w"] // 2, source["y"] + source["h"] // 2,
         target["x"] + 244, target["y"] + 68, "--steps", 20)
    wait_undo("Add Swirl")
    step("set effect amount", "set-value", "--component-name", "Effect swirl", "--exact", "0.22")
    wait_undo("Change effect parameter")
    step("key effect amount", "click", "--name", "Key effect swirl", "--exact")
    wait_undo("Key swirl")
    step("open effect graph", "click", "--name", "Graph", "--class", "osci::TabBar::Tab", "--exact")
    step("effect graph screenshot", "screenshot", "--file", session.artifact_dir / "effect-graph.png")
    step("add graph effect key", "click", "--class", "MotionCurveEditor", "--position", "560,60", "--click-count", 2)
    wait_undo("Add key")
    # Double-clicking an effect adds it to what Properties shows.
    step("append skew", "click", tile("Skew")["ref"], "--click-count", 2)
    wait_undo("Add Skew")
    swirl = find(snapshot(), lambda node: node.get("class") == "MotionEffectStack::Card" and node.get("componentName") == "Swirl effect")["bounds"]
    skew = find(snapshot(), lambda node: node.get("class") == "MotionEffectStack::Card" and node.get("componentName") == "Skew effect")["bounds"]
    # The new effect scrolls into view; its header drags it above Swirl.
    step("reorder effects by their header", "drag-xy", skew["x"] + 60, skew["y"] + 14, skew["x"] + 60, skew["y"] - 60, "--steps", 16)
    wait_undo("Move effect")
    # A track's own effects: select it by its name.
    step("show timeline", "click", "--name", "Timeline", "--class", "osci::TabBar::Tab", "--exact")
    header = find(snapshot(), lambda node: str(node.get("componentName", "")).startswith("Track name ") and node.get("value") == "cube.obj")
    step("select track", "click", header["ref"])
    command("wait-for-value", "--component-name", "Inspector title", "--value", "cube.obj")
    step("add track twist", "click", tile("Twist")["ref"], "--click-count", 2)
    wait_undo("Add Twist")
    step("twist track", "set-value", "--component-name", "Effect twist", "--exact", "0.2")
    # With nothing selected, effects apply to the whole composition.
    step("select nothing", "click", "--class", "MotionTimelineView", "--position", "714,140")
    command("wait-for-value", "--component-name", "Inspector title", "--value", "Composition")
    step("add composition ripple", "click", tile("Ripple")["ref"], "--click-count", 2)
    wait_undo("Add Ripple")
    step("key composition ripple", "click", "--name", "Key effect ripplePhase", "--exact")
    wait_undo("Key phase")
    step("scoped effect screenshot", "screenshot", "--file", session.artifact_dir / "scoped-effects.png")
    step("remove with close button", "click", "--name", "Remove effect", "--exact")
    wait_undo("Remove effect")
    step("undo removal", "click", "--name", "Undo", "--exact")
    command("wait-for-locator", "--component-name", "Effect ripplePhase", "--exact")
    step("effects final screenshot", "screenshot", "--file", session.artifact_dir / "effects-final.png")
    print("Motion scoped effects smoke passed", flush=True)
finally:
    session.stop_app()
