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
    command("wait-for-locator", "--name", "Undo " + text, "--role", "label", "--exact")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", session.window_width or 1100, "--h", session.window_height or 800)
    step("import object", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    library = find(snapshot(), lambda node: node.get("class") == "MotionTabs" and node.get("name") == "Library tabs")
    effectTab = find(library, lambda node: node.get("class") == "MotionTabs::Tab" and node.get("name") == "Effects")
    step("open effects library", "click", effectTab["ref"])
    tree = snapshot()
    source = find(tree, lambda node: str(node.get("class", "")).endswith("Tile") and str(node.get("name", "")).startswith("Swirl"))["bounds"]
    target = find(tree, lambda node: node.get("class") == "MotionTimelineView")["bounds"]
    step("drag effect onto clip", "drag-xy", source["x"] + source["w"] // 2, source["y"] + source["h"] // 2,
         target["x"] + 280, target["y"] + 68, "--steps", 20)
    wait_undo("Add Swirl")
    step("set effect amount", "set-value", "--name", "Effect swirl", "--role", "slider", 0.22)
    wait_undo("Change effect parameter")
    step("key effect amount", "click", "--name", "Key effect swirl", "--exact")
    wait_undo("Key effect parameter")
    step("open effect graph", "click", "--name", "Graph", "--class", "MotionTabs::Tab", "--exact")
    step("effect graph screenshot", "screenshot", "--file", session.artifact_dir / "effect-graph.png")
    step("add graph effect key", "click", "--class", "MotionCurveEditor", "--position", "560,60", "--click-count", 2)
    wait_undo("Add animation key")
    translate = find(snapshot(), lambda node: str(node.get("class", "")).endswith("Tile") and str(node.get("name", "")).startswith("Translate"))
    step("append translate", "click", translate["ref"], "--click-count", 2)
    wait_undo("Add Translate")
    # The chain lists the clip's effects first: Swirl, then Translate.
    step("reorder clip effects", "drag", "--class", "MotionEffectsPanel::Chain", "--position", "60,39", "--dx", 0, "--dy", 32, "--steps", 16)
    wait_undo("Reorder effects")
    step("add to track", "click", "--name", "Add effect to track", "--exact")
    step("add track rotate", "click", "--name", "Rotate", "--role", "menuItem", "--exact")
    wait_undo("Add Rotate")
    step("rotate track", "set-value", "--name", "Effect rotateZ", "--role", "slider", 0.2)
    step("add to composition", "click", "--name", "Add effect to composition", "--exact")
    step("add composition ripple", "click", "--name", "Ripple", "--role", "menuItem", "--exact")
    wait_undo("Add Ripple")
    step("key composition ripple", "click", "--name", "Key effect ripplePhase", "--exact")
    wait_undo("Key effect parameter")
    step("scoped effect screenshot", "screenshot", "--file", session.artifact_dir / "scoped-effects.png")
    # Clip (2 effects), track (1), then the composition's Ripple.
    ripple_y = 26 + 2 * 26 + 6 + 26 + 26 + 6 + 26 + 13
    step("select composition effect", "click", "--class", "MotionEffectsPanel::Chain", "--position", f"80,{ripple_y}")
    step("remove composition effect", "press", "Delete", "--class", "MotionEffectsPanel::Chain")
    wait_undo("Remove effect")
    step("undo removal", "click", "--name", "Undo", "--exact")
    step("reselect composition effect", "click", "--class", "MotionEffectsPanel::Chain", "--position", f"80,{ripple_y}")
    command("wait-for-locator", "--name", "Effect ripplePhase", "--role", "slider", "--exact")
    # The cross at a row's end removes that effect.
    chain = find(snapshot(), lambda node: node.get("class") == "MotionEffectsPanel::Chain")["bounds"]
    step("hover composition effect", "mouse-move", chain["x"] + 80, chain["y"] + ripple_y)
    step("remove with cross", "click-xy", chain["x"] + chain["w"] - 13, chain["y"] + ripple_y)
    wait_undo("Remove effect")
    step("undo cross removal", "click", "--name", "Undo", "--exact")
    step("reselect restored effect", "click", "--class", "MotionEffectsPanel::Chain", "--position", f"80,{ripple_y}")
    command("wait-for-locator", "--name", "Effect ripplePhase", "--role", "slider", "--exact")
    step("effects final screenshot", "screenshot", "--file", session.artifact_dir / "effects-final.png")
    print("Motion scoped effects smoke passed", flush=True)
finally:
    session.stop_app()
