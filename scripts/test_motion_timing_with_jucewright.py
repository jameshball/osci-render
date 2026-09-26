#!/usr/bin/env python3
"""Exercise timeline time displays, beat/frame snapping, meter and graph alignment."""
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
session.launch_app("motion-timing")
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


def choose(item, submenu=None):
    step("open timing menu", "click", "--name", "Time and grid", "--exact")
    if submenu is not None:
        step("open " + submenu, "click", "--name", submenu, "--role", "menuItem", "--exact")
    step("choose " + item, "click", "--name", item, "--role", "menuItem", "--exact")
    wait_undo("Change timeline grid")


def position(expected):
    command("wait-for-locator", "--name", expected, "--role", "label", "--exact")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1100, "--h", 800)
    step("import object", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    choose("Bars / beats")
    choose("1 beat", "Beat grid")
    step("seek to snapped beat", "click", "--class", "MotionTimelineView", "--position", "200,12")
    position("1.2.000")
    step("musical ruler screenshot", "screenshot", "--file", session.artifact_dir / "beat-timeline.png")
    choose("Snap to grid")
    step("seek with grid off", "click", "--class", "MotionTimelineView", "--position", "200,12")
    position("1.1.823")
    choose("Seconds")
    position("0.429s")
    choose("Bars / beats")
    choose("1 beat", "Beat grid")
    tempo = find(snapshot(), lambda node: node.get("componentName") == "Project tempo")
    step("edit tempo", "fill", tempo["ref"], "60")
    step("commit tempo", "press", "Return")
    step("seek at new tempo", "click", "--class", "MotionTimelineView", "--position", "230,12")
    position("1.2.000")
    choose("3/4", "Meter")
    step("seek second bar", "click", "--class", "MotionTimelineView", "--position", "380,12")
    position("2.1.000")
    choose("1/8 triplet", "Beat grid")
    step("seek triplet", "click", "--class", "MotionTimelineView", "--position", "193,12")
    position("1.1.320")
    step("key object", "click", "--name", "Key position.x", "--exact")
    step("show musical graph", "click", "--name", "Graph", "--class", "osci::TabBar::Tab", "--exact")
    step("musical graph screenshot", "screenshot", "--file", session.artifact_dir / "beat-graph.png")
    choose("24 fps", "Frame rate")
    choose("Frames")
    step("show frame timeline", "click", "--name", "Timeline", "--class", "osci::TabBar::Tab", "--exact")
    step("seek frame", "click", "--class", "MotionTimelineView", "--position", "230,12")
    position("21f")
    step("frame ruler screenshot", "screenshot", "--file", session.artifact_dir / "frame-timeline.png")
    step("undo frame display", "click", "--name", "Undo", "--exact")
    step("redo frame display", "click", "--name", "Redo", "--exact")
    position("21f")
    print("Motion timing workflow smoke passed", flush=True)
finally:
    session.stop_app()
