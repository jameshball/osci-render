#!/usr/bin/env python3
"""Open, reject, cancel and supersede Motion projects through the real UI."""
import json
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
session.launch_app("motion-project-loading")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def encoded(data):
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    value = int.from_bytes(data, "little")
    return str(len(data)) + "." + "".join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


def fixture(name, slow=False):
    root = ET.Element("motion-project", schema="1")
    composition = ET.SubElement(root, "composition", name=name, duration="180", bpm="120", fps="30")
    if slow:
        # Deliberately expensive but valid embedded titles exercise cooperative
        # cancellation while the message thread remains available to click.
        text = encoded(("ABCDEFGHIJKLMNOPQRSTUVWXYZ\n" * 80).encode())
        for index in range(80):
            ET.SubElement(composition, "asset", id=str(index + 1), name=f"Title {index}", extension=".txt").text = text
    xml = ET.tostring(root, encoding="utf-8")
    path = session.artifact_dir / (name + ".osci-motion")
    path.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
    return path


def open_file(path):
    session.open_project(path)


def current_name():
    snapshot = json.loads(command("snapshot", "--json", "--full"))
    def walk(value):
        if isinstance(value, dict):
            if value.get("componentName") == "Composition name":
                return value.get("value")
            for child in value.values():
                found = walk(child)
                if found is not None:
                    return found
        elif isinstance(value, list):
            for child in value:
                found = walk(child)
                if found is not None:
                    return found
    return walk(snapshot)


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("size workspace", "resize-window", "--w", 1440, "--h", 900)
    baseline = fixture("Keep this project")
    replacement = fixture("Replacement project")
    slow = fixture("Slow project", True)
    broken = session.artifact_dir / "Broken.osci-motion"
    broken.write_bytes(b"not a project")
    open_file(baseline)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Keep this project", "--timeout-ms", 10000)
    open_file(broken)
    command("wait-for-locator", "--name", "Couldn't open the project", "--exact")
    step("rejected project screenshot", "screenshot", "--file", session.artifact_dir / "failed-load.png")
    step("dismiss failed load", "click", "--name", "OK", "--role", "button", "--exact")
    assert current_name() == "Keep this project"
    step("save preserved project", "press", "command + s", "--class", "MotionEditor")
    assert broken.read_bytes() == b"not a project", "Failed opening changed the save destination"
    open_file(slow)
    command("wait-for-locator", "--name", "Cancel loading", "--role", "button", "--exact")
    step("loading screenshot", "screenshot", "--file", session.artifact_dir / "loading.png")
    step("cancel source preparation", "click", "--name", "Cancel loading", "--role", "button", "--exact")
    assert current_name() == "Keep this project"
    open_file(replacement)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Replacement project", "--timeout-ms", 20000)
    step("save replacement", "press", "command + s", "--class", "MotionEditor")
    data = baseline.read_bytes()
    assert ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition").get("name") == "Keep this project"
    # A newer request supersedes a still-pending load without stale publication.
    open_file(slow)
    command("wait-for-locator", "--name", "Cancel loading", "--role", "button", "--exact")
    open_file(baseline)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Keep this project", "--timeout-ms", 20000)
    step("recovered workspace", "screenshot", "--file", session.artifact_dir / "recovered.png")
    print("Project loading, failure atomicity, cancellation and supersession passed", flush=True)
finally:
    session.stop_app()
