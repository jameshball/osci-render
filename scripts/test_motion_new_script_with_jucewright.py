#!/usr/bin/env python3
"""Write a new script from Add source: the editor opens on a short working script, which bakes as it is."""
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
session.launch_app("motion-new-script")
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


def snapshot():
    return json.loads(command("snapshot", "--json", "--full"))


def names():
    return [node.get("value") for node in nodes(snapshot()) if str(node.get("componentName", "")).startswith("Track name ")]


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("open Add source", "click", "--name", "Add source", "--exact")
    step("menu screenshot", "screenshot", "--file", session.artifact_dir / "menu.png", "--scale", "2")
    step("write a script", "click", "--name", "Write a script...", "--role", "menuItem", "--exact")
    command("wait-for-locator", "--name", "Lua Code Editor", "--role", "editableText", "--exact")
    step("editor screenshot", "screenshot", "--file", session.artifact_dir / "editor.png", "--scale", "2")
    step("bake it", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    session.wait_for_undo("Import source", 60)
    if names() != ["Script.lua"]:
        raise RuntimeError("The new script should land on a track called Script: " + str(names()))
    step("baked screenshot", "screenshot", "--file", session.artifact_dir / "baked.png", "--scale", "2")
    step("open Add source again", "click", "--name", "Add source", "--exact")
    step("write another", "click", "--name", "Write a script...", "--role", "menuItem", "--exact")
    command("wait-for-locator", "--name", "Lua Code Editor", "--role", "editableText", "--exact")
    step("bake the second", "click", "--name", "Bake source", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Script 2.lua", "--class", "juce::Label", "--exact", "--timeout-ms", "60000")
    print("Motion new script passed", flush=True)
finally:
    session.stop_app()
