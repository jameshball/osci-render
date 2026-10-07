#!/usr/bin/env python3
"""Import and reprepare a bounded L-system source through Motion's UI."""
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
session.launch_app("motion-fractal")
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


def fractal_depth():
    snapshot = json.loads(command("snapshot", "--json", "--full"))
    field = next(node for node in nodes(snapshot) if node.get("componentName") == "Fractal depth" and "value" in node)
    return round(float(field["value"]))


def wait_undo(label):
    command("wait-for-locator", "--name", "Undo " + label, "--role", "label", "--exact", "--timeout-ms", "30000")


source = session.root_dir / "Resources" / "fractal" / "koch_snowflake.lsystem"
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", "1440", "--h", "900")
    step("import Koch snowflake", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Prepare fractal", "--class", "juce::TextButton", "--exact", "--timeout-ms", "10000")
    assert fractal_depth() == 3
    step("choose initial fractal depth", "set-value", "--name", "Fractal depth", "--exact", "4")
    assert fractal_depth() == 4
    step("fractal preparation dialog", "screenshot", "--file", session.artifact_dir / "fractal-settings.png")
    step("prepare Koch snowflake", "click", "--name", "Prepare fractal", "--class", "juce::TextButton", "--exact")
    wait_undo("Import object")
    command("wait-for-locator", "--name", "koch_snowflake.lsystem", "--role", "label", "--exact", "--timeout-ms", "30000")

    step("open fractal settings", "click", "--name", "Fractal settings...", "--exact")
    command("wait-for-locator", "--name", "Prepare fractal", "--class", "juce::TextButton", "--exact")
    assert fractal_depth() == 4
    step("increase fractal depth", "set-value", "--name", "Fractal depth", "--exact", "5")
    step("reprepare Koch snowflake", "click", "--name", "Prepare fractal", "--class", "juce::TextButton", "--exact")
    wait_undo("Rebuild source cache")

    step("undo fractal reprepare", "click", "--name", "Undo", "--exact")
    step("reopen restored fractal settings", "click", "--name", "Fractal settings...", "--exact")
    command("wait-for-locator", "--name", "Prepare fractal", "--class", "juce::TextButton", "--exact")
    assert fractal_depth() == 4
    step("close restored fractal settings", "press", "Escape", "--class", "MotionFractalSettingsPanel", "--exact")
    command("wait", "--ms", "500")
    step("final fractal workspace", "screenshot", "--file", session.artifact_dir / "fractal-workspace.png")
    print("Motion fractal import, reprepare and undo workflow passed", flush=True)
finally:
    session.stop_app()
