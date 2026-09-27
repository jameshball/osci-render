#!/usr/bin/env python3
"""Create, connect, place, disconnect and reopen a live Blender source in Motion."""
import base64
import json
import socket
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
session.launch_app("motion-blender")
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
    return next(node for node in nodes(json.loads(command("snapshot", "--json", "--full"))) if node.get("componentName") == name)


def frame():
    data = bytearray()
    def tag(value): data.extend(value.encode("ascii"))
    def integer(value): data.extend(struct.pack("<Q", value))
    def number(value): data.extend(struct.pack("<d", value))
    tag("GPLA    "); integer(2); integer(0); integer(0)
    tag("FILE    "); tag("fCount  "); integer(1); tag("fRate   "); integer(24); tag("DONE    ")
    tag("FRAME   "); tag("focalLen"); number(-1); tag("OBJECTS ")
    tag("OBJECT  "); tag("MATRIX  ")
    for index in range(16): number(1 if index % 5 == 0 else 0)
    tag("DONE    "); tag("STROKES "); tag("STROKE  "); tag("vertexCt"); integer(5); tag("VERTICES")
    for x, y in ((0, .5), (.5, 0), (0, -.5), (-.5, 0), (0, .5)):
        number(x); number(y); number(-1)
    for _ in range(6): tag("DONE    ")
    tag("END GPLA")
    return base64.b64encode(data) + b"\n"


def close_panel():
    step("close Blender settings", "press", "Escape", "--class", "MotionBlenderSourcePanel", "--exact")
    command("wait", "--ms", "400")

project = session.artifact_dir / "live-blender.osci-motion"
root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Live Blender test", duration="12", fps="30", bpm="120")
xml = ET.tostring(root, encoding="utf-8", xml_declaration=True)
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
port = None
for candidate in range(51600, 51700):
    with socket.socket() as probe:
        try:
            probe.bind(("127.0.0.1", candidate))
            port = candidate
            break
        except OSError:
            pass
assert port is not None
sender = None
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Live Blender test", "--timeout-ms", "10000")
    step("resize workspace", "resize-window", "--w", "1440", "--h", "900")
    step("open source menu", "click", "--name", "Add source", "--exact")
    step("choose Blender input", "click", "--name", "Blender live source...", "--role", "menuItem", "--exact")
    step("name Blender input", "fill", "--component-name", "Blender source name", "Live diamond")
    step("set local port", "fill", "--component-name", "Blender port", str(port))
    step("add and listen", "click", "--name", "Add & listen", "--exact")
    command("wait-for-locator", "--name", "Stop listening", "--exact", "--timeout-ms", "10000")
    sender = socket.create_connection(("127.0.0.1", port), timeout=5)
    sender.sendall(frame())
    command("wait-for-value", "--component-name", "Blender connection status", "--value", "Connected | 1 frame received", "--timeout-ms", "10000")
    step("connected source settings", "screenshot", "--file", session.artifact_dir / "blender-connected.png")
    close_panel()
    step("select live asset", "click", "--class", "juce::ListBox::RowComponent", "--name", "Live diamond", "--exact", "--position", "60,20")
    step("insert live asset", "press", "Return", "--component-name", "Motion assets")
    command("wait-for-locator", "--name", "Live diamond", "--role", "label", "--exact", "--timeout-ms", "10000")
    command("wait", "--ms", "600")
    step("live beam and editor", "screenshot", "--file", session.artifact_dir / "blender-live-workspace.png")
    sender.sendall(b"CLOSE\n"); sender.close(); sender = None
    step("open source settings", "click", "--name", "Blender settings...", "--exact")
    command("wait-for-value", "--component-name", "Blender connection status", "--value", "Listening on port " + str(port) + " | Waiting for Blender", "--timeout-ms", "10000")
    step("choose blank on disconnect", "select-option", "--name", "Blender disconnect policy", "--text", "Blank output")
    step("apply disconnect policy", "click", "--name", "Apply settings", "--exact")
    close_panel()
    step("save live source project", "press", "command + s", "--class", "MotionEditor")
    saved = project.read_bytes()
    saved_xml = ET.fromstring(saved[8:8 + struct.unpack("<I", saved[4:8])[0]])
    assert len(saved_xml.findall("./composition/track/clip")) == 1
    source = saved_xml.find("./composition/asset/blender")
    assert source is not None and source.get("port") == str(port) and source.get("disconnect") == "blank"
    step("blank after disconnect", "screenshot", "--file", session.artifact_dir / "blender-disconnected.png")
    session.keep_app = False
    session.stop_app()
    session.launch_app("motion-blender-reopen")
    subprocess.run(["open", "-a", str(session.app_path), str(project)], check=True)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Live Blender test", "--timeout-ms", "10000")
    command("wait", "--ms", "600")
    step("select reopened source", "click", "--class", "juce::ListBox::RowComponent", "--name", "Live diamond", "--exact", "--position", "60,20")
    step("reopen Blender settings", "click", "--name", "Blender settings...", "--exact")
    command("wait-for-value", "--component-name", "Blender connection status", "--value", "Offline - start listening to connect Blender", "--timeout-ms", "10000")
    assert str(port) in str(field("Blender port"))
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("127.0.0.1", port))
    step("compact workspace", "resize-window", "--w", "1100", "--h", "700")
    step("reopened offline settings", "screenshot", "--file", session.artifact_dir / "blender-reopened.png")
    print("Live Blender creation, socket input, timeline placement, disconnect policy and offline reopen passed", flush=True)
finally:
    if sender is not None:
        sender.close()
    session.keep_app = keep
    session.stop_app()
