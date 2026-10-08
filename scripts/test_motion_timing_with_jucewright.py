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
    session.wait_for_undo(text)


def choose(item, submenu=None):
    step("open timing menu", "click", "--name", "Timing", "--class", "juce::MenuBarComponent::AccessibleItemComponent", "--exact")
    if submenu is not None:
        step("open " + submenu, "click", "--name", submenu, "--role", "menuItem", "--exact")
    step("choose " + item, "click", "--name", item, "--role", "menuItem", "--exact")
    # Meter and frame rate are edits; display and snapping are view options
    # with no undo step.
    if submenu in ("Meter", "Frame rate"):
        wait_undo("Change meter" if submenu == "Meter" else "Change frame rate")
    else:
        command("wait", "--ms", 250)


def position(expected):
    command("wait-for-locator", "--name", expected, "--class", "juce::Label", "--exact")


def enter_position(value, expected=None, reject=False, cancel=False):
    target = find(snapshot(), lambda n: n.get("componentName") == "Timeline position")
    step("open position entry", "click", target["ref"], "--click-count", "2")
    target = find(snapshot(), lambda n: n.get("componentName") == "Timeline position")
    editor = find(target, lambda n: n.get("class") == "juce::TextEditor")
    assert editor is not None, "Position did not enter text edit mode"
    step("type position " + value, "fill", editor["ref"], value)
    step("finish position entry", "press", "Escape" if cancel else "Return")
    if reject:
        command("wait-for-locator", "--text", "Enter a position from 0 to")
        step("dismiss invalid position", "click", "--name", "Dismiss message", "--exact")
    if expected is not None:
        position(expected)


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1100, "--h", 800)
    step("import object", "drop-files", "--file", session.root_dir / "Resources/models/cube.obj", "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "cube.obj", "--role", "label", "--exact")
    enter_position("90s", "90.000s")
    enter_position("1:30.25", "90.250s")
    enter_position("181s", "90.250s", reject=True)
    enter_position("30s", "90.250s", cancel=True)
    choose("Frames")
    enter_position("240", "240f")
    enter_position("48f", "48f")
    choose("Bars / beats")
    enter_position("3.2.480", "3.2.480")
    enter_position("240f", "5.1.000")
    enter_position("0.2.000", "5.1.000", reject=True)
    choose("Seconds")
    enter_position("0s", "0.000s")
    choose("Bars / beats")
    choose("1 beat", "Beat grid")
    # The playhead moves freely: away from clip edges, keys, markers and the
    # loop, a ruler click is not pulled to the beat grid, even with snapping on.
    step("seek between beats", "click", "--class", "MotionTimelineView", "--position", "250,12")
    position("1.1.823")
    step("musical ruler screenshot", "screenshot", "--file", session.artifact_dir / "beat-timeline.png")
    choose("Seconds")
    position("0.429s")
    choose("Bars / beats")
    tempo = find(snapshot(), lambda node: node.get("componentName") == "Project tempo")
    step("edit tempo", "fill", tempo["ref"], "60")
    step("commit tempo", "press", "Return")
    # Typed positions read bars.beats under each tempo and meter.
    enter_position("1.2.000", "1.2.000")
    choose("3/4", "Meter")
    enter_position("2.1.000", "2.1.000")
    choose("1/8 triplet", "Beat grid")
    enter_position("1.1.320", "1.1.320")
    step("key object", "click", "--name", "Key position", "--exact")
    step("show musical graph", "click", "--name", "Graph", "--class", "osci::TabBar::Tab", "--exact")
    step("musical graph screenshot", "screenshot", "--file", session.artifact_dir / "beat-graph.png")
    choose("24 fps", "Frame rate")
    choose("Frames")
    step("show frame timeline", "click", "--name", "Timeline", "--class", "osci::TabBar::Tab", "--exact")
    step("seek frame", "click", "--class", "MotionTimelineView", "--position", "280,12")
    position("21f")
    step("frame ruler screenshot", "screenshot", "--file", session.artifact_dir / "frame-timeline.png")
    # Undo reverts the frame rate but keeps the display, like any view option.
    step("undo frame rate", "click", "--name", "Undo", "--exact")
    command("wait", "--ms", 300)
    step("redo frame rate", "click", "--name", "Redo", "--exact")
    position("21f")
    print("Motion timing workflow smoke passed", flush=True)
finally:
    session.stop_app()
