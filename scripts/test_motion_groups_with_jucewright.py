#!/usr/bin/env python3
"""Exercise nested groups, shared transforms/effects, folding and deletion undo."""
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
session.launch_app("motion-groups")
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


def header_id(label):
    node = find(snapshot(), lambda node: str(node.get("componentName", "")).startswith("Track name ") and node.get("value") == label)
    if node is None:
        raise RuntimeError("Missing timeline header " + label)
    return node["componentName"].split()[-1]


def header_bounds(id):
    return find(snapshot(), lambda node: node.get("name") == "Reorder track " + id)["bounds"]


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1100, "--h", 800)
    step("import object", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    track = header_id("cube.obj")
    step("track menu", "click", "--name", "Reorder track " + track, "--exact")
    step("group track", "click", "--name", "Group track", "--role", "menuItem", "--exact")
    wait_undo("Create group")
    group = header_id("Group 1")
    source = header_bounds(track)
    timeline = find(snapshot(), lambda node: node.get("class") == "MotionTimelineView")["bounds"]
    step("reject track drop on ruler", "drag-xy", source["x"] + source["w"] // 2, source["y"] + source["h"] // 2,
         timeline["x"] + 20, timeline["y"] + 8, "--steps", 20)
    if header_bounds(track) != source:
        raise RuntimeError("Ruler drop changed track order or parent")
    position = find(snapshot(), lambda node: node.get("componentName") == "position.x")
    step("translate group", "set-value", position["ref"], "0.35")
    wait_undo("Change property")
    step("key group position", "click", "--name", "Key position", "--exact")
    wait_undo("Set keyframe")
    step("fold group", "click", "--name", "Fold group " + group, "--exact")
    hidden = find(snapshot(), lambda node: node.get("name") == "Reorder track " + track and node.get("visible"))
    if hidden is not None:
        raise RuntimeError("Collapsed group kept child track visible")
    step("unfold group", "click", "--name", "Fold group " + group, "--exact")
    step("group menu", "click", "--name", "Reorder track " + group, "--exact")
    step("create nested group", "click", "--name", "Add nested group", "--role", "menuItem", "--exact")
    wait_undo("Create group")
    nested = header_id("Group 2")
    source, target = header_bounds(track), header_bounds(nested)
    step("drag track into nested group", "drag-xy", source["x"] + source["w"] // 2, source["y"] + source["h"] // 2,
         target["x"] + target["w"] // 2, target["y"] + target["h"] // 2, "--steps", 20)
    wait_undo("Reorder track")
    if header_bounds(track)["x"] <= source["x"]:
        raise RuntimeError("Track did not become a nested child")
    step("nested group screenshot", "screenshot", "--file", session.artifact_dir / "nested-groups.png")
    library = find(snapshot(), lambda node: node.get("class") == "MotionTabs" and node.get("name") == "Library tabs")
    tab = find(library, lambda node: node.get("class") == "MotionTabs::Tab" and node.get("name") == "Effects")
    step("effects library", "click", tab["ref"])
    source = find(snapshot(), lambda node: node.get("role") == "listItem" and node.get("name") == "Scale")["bounds"]
    target = header_bounds(group)
    step("effect on group", "drag-xy", source["x"] + source["w"] // 2, source["y"] + source["h"] // 2,
         target["x"] + target["w"] // 2, target["y"] + target["h"] // 2, "--steps", 20)
    wait_undo("Add Scale")
    command("wait-for-locator", "--name", "Effect scaleX", "--role", "slider", "--exact")
    step("group effects screenshot", "screenshot", "--file", session.artifact_dir / "group-effects.png")
    step("empty grouped track menu", "click", "--name", "Reorder track " + group, "--exact")
    step("add empty grouped track", "click", "--name", "Add track to group", "--role", "menuItem", "--exact")
    wait_undo("Add track")
    empty = header_id("New track")
    source = find(snapshot(), lambda node: node.get("role") == "listItem" and node.get("name") == "Translate")["bounds"]
    target = header_bounds(empty)
    step("effect on empty grouped track", "drag-xy", source["x"] + source["w"] // 2, source["y"] + source["h"] // 2,
         target["x"] + target["w"] // 2, target["y"] + target["h"] // 2, "--steps", 20)
    wait_undo("Add Translate")
    step("group scope from empty track", "select-option", "--name", "Effect scope", "--id", 4)
    step("add effect to empty track parent", "click", "--name", "Swirl", "--role", "listItem", "--click-count", 2)
    wait_undo("Add Swirl")
    step("delete parent menu", "click", "--name", "Reorder track " + group, "--exact")
    step("delete group", "click", "--name", "Delete group and its tracks", "--role", "menuItem", "--exact")
    wait_undo("Delete group")
    if find(snapshot(), lambda node: node.get("name") == "Reorder track " + track) is not None:
        raise RuntimeError("Group deletion left descendant track")
    step("undo group deletion", "click", "--name", "Undo", "--exact")
    command("wait-for-locator", "--name", "Reorder track " + track, "--exact")
    step("restored group screenshot", "screenshot", "--file", session.artifact_dir / "group-restored.png")
    print("Motion group workflow smoke passed", flush=True)
finally:
    session.stop_app()
