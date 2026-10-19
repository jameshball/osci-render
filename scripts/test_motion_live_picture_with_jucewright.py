#!/usr/bin/env python3
"""A picture another app shares (Syphon, Spout) is added from Add source > Live, drawn live, traced as its settings say, and captured to export."""
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
session.launch_app("motion-live-picture")
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


import time


def find(predicate):
    for node in nodes(snapshot()):
        if predicate(node):
            return node
    return None


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("open Add source", "click", "--name", "Add source", "--exact")
    step("open Live", "click", "--name", "Live", "--role", "menuItem", "--exact")
    time.sleep(.4)
    step("menu screenshot", "screenshot", "--file", session.artifact_dir / "menu.png", "--scale", "2")
    sender = find(lambda n: n.get("role") == "menuItem" and "Simple Server" in str(n.get("name", "")))
    if sender is None:
        raise RuntimeError("The Live menu should list Simple Server while it shares its picture")
    step("add the sender", "click", "--name", sender["name"], "--role", "menuItem", "--exact")
    session.wait_for_undo("Add object clip")
    time.sleep(4)
    step("live screenshot", "screenshot", "--file", session.artifact_dir / "live.png", "--scale", "2")
    status = find(lambda n: str(n.get("value") or n.get("name") or "").startswith("Live"))
    if status is None:
        raise RuntimeError("Sources should say the picture is live")
    session.hover_selected_source()
    step("open its settings", "click", "--name", "Syphon settings...", "--exact")
    command("wait-for-locator", "--name", "Done", "--exact")
    time.sleep(1.5)
    step("settings screenshot", "screenshot", "--file", session.artifact_dir / "settings.png", "--scale", "2")
    step("Scanlines", "set-value", "--name", "Image trace mode", "--exact", "Scanlines")
    time.sleep(2)
    step("Hilbert", "set-value", "--name", "Image trace mode", "--exact", "Hilbert")
    time.sleep(2)
    step("hilbert screenshot", "screenshot", "--file", session.artifact_dir / "hilbert.png", "--scale", "2")
    step("record", "click", "--name", "Record capture", "--exact")
    time.sleep(3)
    step("recording screenshot", "screenshot", "--file", session.artifact_dir / "recording.png", "--scale", "2")
    step("stop and keep", "click", "--name", "Stop and keep", "--exact")
    # Kept: its settings close by themselves, so what was kept shows.
    session.wait_for_undo("Capture live source", 60)
    time.sleep(.5)
    step("captured screenshot", "screenshot", "--file", session.artifact_dir / "captured.png", "--scale", "2")
    # The sending app quitting mid-capture ends it there, and it is kept by itself.
    step("select live source", "click", "--class", "juce::ListBox::RowComponent", "--name", sender["name"], "--exact", "--position", "60,20")
    session.hover_selected_source()
    step("reopen its settings", "click", "--name", "Syphon settings...", "--exact")
    step("record again", "click", "--name", "Record capture", "--exact")
    time.sleep(2)
    server = subprocess.run(["ps", "-axo", "comm="], capture_output=True, text=True).stdout
    server = next(line for line in server.splitlines() if line.endswith("/Simple Server"))
    # Quit as a person quits it, so it tells Syphon it has gone (a sender that
    # only stops sending looks paused, and its capture goes on).
    subprocess.run(["osascript", "-e", 'quit app "Simple Server"'], check=True)
    try:
        command("wait-for-text", "stopped sending, so its capture ended there.", "--timeout-ms", "20000")
        step("sender gone screenshot", "screenshot", "--file", session.artifact_dir / "sender-gone.png", "--scale", "2")
    finally:
        subprocess.Popen(["open", "-g", "-a", server.split("/Contents/MacOS/")[0]])
    print("Motion live picture passed", flush=True)
finally:
    session.stop_app()
