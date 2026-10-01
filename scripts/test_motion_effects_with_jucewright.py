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
    library = find(snapshot(), lambda node: node.get("class") == "osci::TabBar" and node.get("name") == "Library tabs")
    effectTab = find(library, lambda node: node.get("class") == "osci::TabBar::Tab" and node.get("name") == "Effects")
    step("open effects library", "click", effectTab["ref"])
    tree = snapshot()
    source = find(tree, lambda node: node.get("role") == "listItem" and node.get("name") == "Swirl")["bounds"]
    target = find(tree, lambda node: node.get("class") == "MotionTimelineView")["bounds"]
    step("drag effect onto clip", "drag-xy", source["x"] + source["w"] // 2, source["y"] + source["h"] // 2,
         target["x"] + 280, target["y"] + 46, "--steps", 20)
    wait_undo("Add Swirl")
    step("set effect amount", "set-value", "--name", "Effect swirl", "--role", "slider", 0.22)
    wait_undo("Change effect parameter")
    step("key effect amount", "click", "--name", "Key effect swirl", "--exact")
    wait_undo("Key effect parameter")
    step("open effect graph", "click", "--name", "Graph", "--class", "osci::TabBar::Tab", "--exact")
    step("effect graph screenshot", "screenshot", "--file", session.artifact_dir / "effect-graph.png")
    step("add graph effect key", "click", "--class", "MotionCurveEditor", "--position", "560,60", "--click-count", 2)
    wait_undo("Add animation key")
    step("append translate", "click", "--name", "Translate", "--role", "listItem", "--click-count", 2)
    wait_undo("Add Translate")
    tree = snapshot()
    stack = find(tree, lambda node: node.get("class") == "juce::ListBox" and node.get("name") == "Effect stack")["bounds"]
    step("reorder effect stack", "drag-xy", stack["x"] + 60, stack["y"] + 15, stack["x"] + 60, stack["y"] + 47, "--steps", 16)
    wait_undo("Reorder effects")
    step("choose track scope", "select-option", "--name", "Effect scope", "--id", 2)
    step("add track rotate", "click", "--name", "Rotate", "--role", "listItem", "--click-count", 2)
    wait_undo("Add Rotate")
    step("rotate track", "set-value", "--name", "Effect rotateZ", "--role", "slider", 0.2)
    step("choose composition scope", "select-option", "--name", "Effect scope", "--id", 3)
    step("add composition ripple", "click", "--name", "Ripple", "--role", "listItem", "--click-count", 2)
    wait_undo("Add Ripple")
    step("key composition ripple", "click", "--name", "Key effect ripplePhase", "--exact")
    wait_undo("Key effect parameter")
    step("scoped effect screenshot", "screenshot", "--file", session.artifact_dir / "scoped-effects.png")
    step("focus effect stack", "click", "--name", "Effect stack", "--class", "juce::ListBox", "--exact")
    step("remove composition effect", "press", "Delete", "--name", "Effect stack", "--class", "juce::ListBox", "--exact")
    wait_undo("Remove effect")
    step("undo removal", "click", "--name", "Undo", "--exact")
    command("wait-for-locator", "--name", "Effect ripplePhase", "--role", "slider", "--exact")
    # The cross at a row's end removes that effect.
    stack = find(snapshot(), lambda node: node.get("class") == "juce::ListBox" and node.get("name") == "Effect stack")["bounds"]
    step("remove with cross", "click-xy", stack["x"] + stack["w"] - 13, stack["y"] + 14)
    wait_undo("Remove effect")
    step("undo cross removal", "click", "--name", "Undo", "--exact")
    command("wait-for-locator", "--name", "Effect ripplePhase", "--role", "slider", "--exact")
    step("effects final screenshot", "screenshot", "--file", session.artifact_dir / "effects-final.png")
    print("Motion scoped effects smoke passed", flush=True)
finally:
    session.stop_app()
