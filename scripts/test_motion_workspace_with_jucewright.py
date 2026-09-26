#!/usr/bin/env python3
"""Exercise the first Motion import/animation workspace in an isolated profile."""
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
session.launch_app("motion-workspace")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def find_node(value, predicate):
    if isinstance(value, dict):
        if predicate(value):
            return value
        for child in value.values():
            found = find_node(child, predicate)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find_node(child, predicate)
            if found is not None:
                return found
    return None


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    if session.window_width is not None and session.window_height is not None:
        step("resize initial workspace", "resize-window", "--w", session.window_width, "--h", session.window_height)
    step("import cube", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    step("key position", "click", "--name", "Key position.x", "--exact")
    step("play timeline", "click", "--name", "Play", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Pause", "--class", "juce::TextButton", "--exact")
    step("pause timeline", "click", "--name", "Pause", "--class", "juce::TextButton", "--exact")
    step("motion workspace", "screenshot", "--file", session.artifact_dir / "motion-workspace.png")
    step("undo key", "click", "--name", "Undo", "--exact")
    step("redo key", "click", "--name", "Redo", "--exact")
    tree = json.loads(command("snapshot", "--json", "--full"))
    asset = find_node(tree, lambda node: node.get("role") == "listItem" and node.get("name") == "cube.obj")
    timeline = find_node(tree, lambda node: node.get("class") == "MotionTimelineView")
    if asset is None or timeline is None:
        raise RuntimeError("Asset library or timeline missing")
    source = asset["bounds"]
    target = timeline["bounds"]
    step("drag reusable asset", "drag-xy", source["x"] + source["w"] // 2, source["y"] + source["h"] // 2,
         target["x"] + 170 + 6 * 70, target["y"] + 46, "--steps", 20)
    command("wait-for-locator", "--name", "Undo Add object clip", "--role", "label", "--exact")
    step("asset placement screenshot", "screenshot", "--file", session.artifact_dir / "motion-asset-placement.png")
    step("undo asset placement", "click", "--name", "Undo", "--exact")
    step("select original clip", "click", "--class", "MotionTimelineView", "--position", "250,46")
    step("choose slip tool", "press", "S", "--class", "MotionTimelineView")
    step("slip clip", "drag", "--class", "MotionTimelineView", "--position", "250,46", "--dx", 70, "--dy", 0)
    command("wait-for-locator", "--name", "Undo Slip clip", "--role", "label", "--exact")
    step("undo slip", "click", "--name", "Undo", "--exact")
    step("choose stretch tool", "press", "R", "--class", "MotionTimelineView")
    step("stretch clip", "drag", "--class", "MotionTimelineView", "--position", "250,46", "--dx", 70, "--dy", 0)
    command("wait-for-locator", "--name", "Undo Stretch clip", "--role", "label", "--exact")
    step("undo stretch", "click", "--name", "Undo", "--exact")
    step("choose move tool", "press", "V", "--class", "MotionTimelineView")
    step("open graph", "click", "--name", "Graph", "--class", "osci::TabBar::Tab", "--exact")
    step("add graph key", "click", "--class", "MotionCurveEditor", "--position", "560,60", "--click-count", 2)
    command("wait-for-locator", "--name", "Undo Add animation key", "--role", "label", "--exact")
    step("graph screenshot", "screenshot", "--file", session.artifact_dir / "motion-graph.png")
    step("delete graph key", "press", "Delete", "--class", "MotionCurveEditor")
    command("wait-for-locator", "--name", "Undo Delete animation key", "--role", "label", "--exact")
    step("undo graph deletion", "click", "--name", "Undo", "--exact")
    before_resize = json.loads(command("snapshot", "--json", "--full"))
    preview_before = find_node(before_resize, lambda node: node.get("class") == "MotionCompositionView")["bounds"]
    step("resize preview panes", "drag", "--name", "Resize preview panels", "--class", "osci::PanelDivider", "--exact", "--position", "3,20", "--dx", 60, "--dy", 0)
    after_resize = json.loads(command("snapshot", "--json", "--full"))
    preview_after = find_node(after_resize, lambda node: node.get("class") == "MotionCompositionView")["bounds"]
    if preview_after["w"] <= preview_before["w"] + 40:
        raise RuntimeError("Preview divider did not resize the composition panel")
    graph_before = find_node(after_resize, lambda node: node.get("class") == "MotionCurveEditor")["bounds"]
    step("resize timeline pane", "drag", "--name", "Resize timeline", "--class", "osci::PanelDivider", "--exact", "--position", "20,3", "--dx", 0, "--dy", -30)
    after_resize = json.loads(command("snapshot", "--json", "--full"))
    graph_after = find_node(after_resize, lambda node: node.get("class") == "MotionCurveEditor")["bounds"]
    if graph_after["h"] <= graph_before["h"] + 20:
        raise RuntimeError("Timeline divider did not resize the animation panel")
    step("open camera inspector", "click", "--name", "Camera", "--class", "osci::TabBar::Tab", "--exact")
    step("add camera", "click", "--name", "Add", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Key camera position.x", "--exact")
    step("key camera", "click", "--name", "Key camera position.x", "--exact")
    step("cut camera", "click", "--name", "Cut here", "--exact")
    command("wait-for-locator", "--name", "Undo Cut to camera", "--role", "label", "--exact")
    step("camera graph screenshot", "screenshot", "--file", session.artifact_dir / "motion-camera-graph.png")
    step("compact workspace", "resize-window", "--w", "1100", "--h", "700")
    step("compact screenshot", "screenshot", "--file", session.artifact_dir / "motion-compact.png")
    step("import animated source", "drop-files", "--file", session.root_dir / "Resources/lottie/spinning_squares.lottie", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "spinning_squares.lottie", "--role", "listItem", "--exact", "--timeout-ms", 120000)
    command("wait-for-locator", "--name", "spinning_squares.lottie", "--role", "label", "--exact")
    step("show animated timeline", "click", "--name", "Timeline", "--class", "osci::TabBar::Tab", "--exact")
    step("scrub animation first pose", "click", "--class", "MotionTimelineView", "--position", "190,12")
    step("animated first pose", "screenshot", "--file", session.artifact_dir / "motion-animation-first.png")
    step("scrub animation second pose", "click", "--class", "MotionTimelineView", "--position", "225,12")
    step("animated second pose", "screenshot", "--file", session.artifact_dir / "motion-animation-second.png")
    step("undo animation import", "click", "--name", "Undo", "--exact")
    tree = json.loads(command("snapshot", "--json", "--full"))
    if find_node(tree, lambda node: node.get("role") == "listItem" and node.get("name") == "spinning_squares.lottie") is not None:
        raise RuntimeError("Undo did not remove the imported animated asset")
    step("redo animation import", "click", "--name", "Redo", "--exact")
    command("wait-for-locator", "--name", "spinning_squares.lottie", "--role", "listItem", "--exact")
    print("Motion workspace smoke passed", flush=True)
finally:
    session.stop_app()
