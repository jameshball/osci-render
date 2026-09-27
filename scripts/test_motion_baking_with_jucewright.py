#!/usr/bin/env python3
"""Exercise Lua import settings, validation, shared-source rebuilding and undo."""
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
session.launch_app("motion-baking")
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


def field(name):
    tree = json.loads(command("snapshot", "--json", "--full"))
    return next(node for node in nodes(tree) if node.get("class") == "juce::TextEditor" and node.get("componentName") == name)


def duration(value):
    step("set bake duration " + value, "fill", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact", value)


def wait_undo(label):
    command("wait-for-locator", "--name", "Undo " + label, "--role", "label", "--exact")


fixture = session.artifact_dir / "Orbit.lua"
fixture.write_text("return {0.7*math.cos(phase),0.7*math.sin(phase),0,1,0.3,0.1}\n")
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("import Lua", "drop-files", "--file", fixture, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Bake duration", "--class", "juce::TextEditor", "--exact")
    duration("0")
    tree = json.loads(command("snapshot", "--json", "--full"))
    bake = next(node for node in nodes(tree) if node.get("class") == "juce::TextButton" and node.get("componentName") == "Bake source")
    assert not bake.get("enabled", True), "Invalid duration must disable baking"
    duration("1")
    step("bake settings screenshot", "screenshot", "--file", session.artifact_dir / "settings.png")
    step("prepare Lua", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    wait_undo("Import object")
    command("wait", "--ms", 600)
    step("baked workspace screenshot", "screenshot", "--file", session.artifact_dir / "workspace.png")
    step("open source bake settings", "click", "--name", "Edit Lua...", "--exact")
    assert float(field("Bake duration").get("value", "nan")) == 1
    duration("2")
    step("rebuild source", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    wait_undo("Rebuild source cache")
    step("inspect rebuilt settings", "click", "--name", "Edit Lua...", "--exact")
    assert float(field("Bake duration").get("value", "nan")) == 2
    step("close rebuild settings", "press", "Escape", "--class", "MotionBakeSettingsPanel", "--exact")
    step("undo source rebuild", "click", "--name", "Undo", "--exact")
    step("inspect restored source", "click", "--name", "Edit Lua...", "--exact")
    assert float(field("Bake duration").get("value", "nan")) == 1
    step("close restored settings", "press", "Escape", "--class", "MotionBakeSettingsPanel", "--exact")
    command("wait", "--ms", 600)
    tree = json.loads(command("snapshot", "--json", "--full"))
    assert not any(node.get("class") == "MotionBakeSettingsPanel" for node in nodes(tree)), "Settings overlay did not close"
    step("compact workspace", "resize-window", "--w", 1100, "--h", 700)
    step("compact baked workspace screenshot", "screenshot", "--file", session.artifact_dir / "compact.png")
    print("Motion baking workflow passed", flush=True)
finally:
    session.stop_app()
