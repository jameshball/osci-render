#!/usr/bin/env python3
"""Exercise Motion track controls, naming and native layer reordering."""
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
session.launch_app("motion-tracks")
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
    command("wait-for-locator", "--name", "Undo " + text, "--role", "label", "--exact", "--hidden")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1100, "--h", 800)
    step("import object", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    step("add empty track", "click", "--name", "Add track", "--exact")
    wait_undo("Add track")
    tree = snapshot()
    first = find(tree, lambda node: str(node.get("componentName", "")).startswith("Track name ") and node.get("value") == "cube.obj")
    track_id = first["componentName"].split()[-1]
    step("mute object track", "click", "--name", "Mute track " + track_id, "--exact")
    wait_undo("Toggle track mute")
    step("muted output", "screenshot", "--file", session.artifact_dir / "muted-track.png")
    step("undo mute", "click", "--name", "Undo", "--exact")
    step("solo object track", "click", "--name", "Solo track " + track_id, "--exact")
    wait_undo("Toggle track solo")
    label = find(snapshot(), lambda node: node.get("componentName") == "Track name " + track_id)
    step("rename object track", "fill", label["ref"], "Main geometry")
    step("commit track name", "press", "Return")
    wait_undo("Rename track")
    tree = snapshot()
    grip = find(tree, lambda node: node.get("name") == "Reorder track " + track_id)["bounds"]
    timeline = find(tree, lambda node: node.get("class") == "MotionTimelineView")["bounds"]
    step("reorder layers", "drag-xy", grip["x"] + grip["w"] // 2, grip["y"] + grip["h"] // 2,
         timeline["x"] + 20, timeline["y"] + 109, "--steps", 20)
    wait_undo("Reorder track")
    after = find(snapshot(), lambda node: node.get("name") == "Reorder track " + track_id)["bounds"]
    if after["y"] <= grip["y"]:
        raise RuntimeError("Track header did not move down")
    step("undo reorder", "click", "--name", "Undo", "--exact")
    restored = find(snapshot(), lambda node: node.get("name") == "Reorder track " + track_id)["bounds"]
    if restored["y"] != grip["y"]:
        raise RuntimeError("Track order did not restore")
    step("track actions", "click", "--name", "Reorder track " + track_id, "--exact")
    step("delete populated track", "click", "--name", "Delete track", "--role", "menuItem", "--exact")
    wait_undo("Delete track")
    if find(snapshot(), lambda node: node.get("name") == "Reorder track " + track_id) is not None:
        raise RuntimeError("Deleted track header remains")
    step("undo track deletion", "click", "--name", "Undo", "--exact")
    command("wait-for-locator", "--name", "Reorder track " + track_id, "--exact")
    step("tracks screenshot", "screenshot", "--file", session.artifact_dir / "track-controls.png")
    print("Motion track controls smoke passed", flush=True)
finally:
    session.stop_app()
