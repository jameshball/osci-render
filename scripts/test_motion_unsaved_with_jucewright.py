#!/usr/bin/env python3
"""Verify that opening another project asks before unsaved changes are lost."""
import struct
import subprocess
import xml.etree.ElementTree as ET
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-unsaved")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def project(name):
    root = ET.Element("motion-project", schema="1")
    ET.SubElement(root, "composition", name=name, duration="20", bpm="120", fps="30")
    xml = ET.tostring(root, encoding="utf-8")
    path = session.artifact_dir / f"{name}.osci-motion"
    path.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
    return path


def wait_name(name):
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", name, "--timeout-ms", "10000")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    first, second = project("First"), project("Second")
    session.open_project(first)
    wait_name("First")
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    # Nothing changed: another project opens straight away.
    session.open_project(second)
    wait_name("Second")
    step("add rectangle", "click", "--name", "Add source", "--exact")
    step("open generate", "click", "--name", "Generate", "--role", "menuItem", "--exact")
    step("choose rectangle", "click", "--name", "Rectangle", "--role", "menuItem", "--exact")
    session.wait_for_undo("Add rectangle")
    # Changed: it asks first, and Cancel keeps the project.
    session.open_project(first)
    command("wait-for-locator", "--name", "Discard", "--exact")
    step("asked", "screenshot", "--file", session.artifact_dir / "discard.png")
    step("cancel", "click", "--name", "Cancel", "--exact")
    wait_name("Second")
    # Saved, it no longer asks.
    step("save", "press", "command + s", "--class", "MotionEditor")
    session.open_project(first)
    wait_name("First")
    # Changed again and discarded, the other opens.
    step("add rectangle again", "click", "--name", "Add source", "--exact")
    step("open generate again", "click", "--name", "Generate", "--role", "menuItem", "--exact")
    step("choose rectangle again", "click", "--name", "Rectangle", "--role", "menuItem", "--exact")
    session.wait_for_undo("Add rectangle")
    session.open_project(second)
    command("wait-for-locator", "--name", "Discard", "--exact")
    step("discard", "click", "--name", "Discard", "--exact")
    wait_name("Second")
    print("Motion unsaved changes workflow passed", flush=True)
finally:
    session.stop_app()
