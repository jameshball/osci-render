#!/usr/bin/env python3
"""Verify shared canvas editing, validation and persistence through the app UI."""
import struct
import subprocess
import xml.etree.ElementTree as ET
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession
from create_motion_export_fixture import create_fixture

session = BrowserSession(parse_args())
source = create_fixture(session.artifact_dir / 'Canvas source.osci-motion')
project = session.artifact_dir / 'Canvas review.osci-motion'
data = source.read_bytes()
fixture = ET.fromstring(data[8:8 + struct.unpack('<I', data[4:8])[0]])
recording = ET.SubElement(ET.SubElement(fixture, 'recording'), 'recordingSettings')
rate = ET.SubElement(recording, 'frameRate', id='frameRate')
ET.SubElement(rate, 'parameter', id='frameRate', value='60')
payload = ET.tostring(fixture, encoding='utf-8')
project.write_bytes(struct.pack('<II', 0x21324356, len(payload)) + payload + b'\0')
assert session.find_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app('canvas-review')
session.keep_app = keep


def step(label, *args):
    assert session.run_step(label, session.cli(*args)), label


def command(*args):
    return subprocess.check_output(session.cli(*args), text=True)


def canvas_value(name, value):
    step('set ' + name, 'set-value', '--name', name, '--exact', str(value))


def check_saved():
    data = project.read_bytes()
    root = ET.fromstring(data[8:8 + struct.unpack('<I', data[4:8])[0]])
    settings = root.find('recording/recordingSettings')
    assert float(settings.find('canvasWidth/parameter').get('value')) == 1280
    assert float(settings.find('canvasHeight/parameter').get('value')) == 720
    assert float(settings.find('frameRate/parameter').get('value')) == 24


try:
    session.open_project(project)
    raw = source.read_bytes()
    root = ET.fromstring(raw[8:8 + struct.unpack('<I', raw[4:8])[0]])
    command('wait-for-value', '--component-name', 'Composition name', '--hidden', '--value', root.find('composition').get('name'), '--timeout-ms', 120000)
    step('compact workspace', 'resize-window', '--w', 1200, '--h', 800)
    step('canvas entry', 'click', '--name', 'Output canvas', '--exact')
    step('portrait preset', 'select-option', '--name', 'Canvas preset', '--exact', '--index', 3)
    step('portrait width', 'wait-for-value', '--name', 'Video width', '--exact', '--value', '1080')
    step('portrait height', 'wait-for-value', '--name', 'Video height', '--exact', '--value', '1920')
    step('landscape preset', 'select-option', '--name', 'Canvas preset', '--exact', '--index', 2)
    step('landscape width', 'wait-for-value', '--name', 'Video width', '--exact', '--value', '1920')
    canvas_value('Video width', 127)
    step('reject invalid canvas', 'click', '--name', 'Apply canvas', '--exact')
    step('validation message', 'wait-for-text', 'Even sizes, 128-4096 px')
    canvas_value('Video width', 1280)
    canvas_value('Video height', 720)
    step('canvas dialog', 'screenshot', '--file', session.artifact_dir / 'canvas-dialog.png')
    step('apply delivery canvas', 'click', '--name', 'Apply canvas', '--exact')
    step('save delivery canvas', 'press', 'command + s', '--class', 'MotionEditor')
    check_saved()
    step('output header', 'screenshot', '--file', session.artifact_dir / 'canvas-workspace.png')
    step('open file menu', 'click', '--name', 'File', '--exact')
    step('open export', 'click', '--name', 'Export video...', '--exact')
    step('export uses canvas width', 'wait-for-value', '--name', 'Video width', '--exact', '--value', '1280')
    step('export uses canvas height', 'wait-for-value', '--name', 'Video height', '--exact', '--value', '720')
    step('export shared preset', 'select-option', '--name', 'Canvas preset', '--exact', '--index', 1)
    step('export square width', 'wait-for-value', '--name', 'Video width', '--exact', '--value', '1024')
    step('export dialog', 'screenshot', '--file', session.artifact_dir / 'export-canvas.png')
    step('cancel export changes', 'press', 'Escape')
    step('save after cancelled export', 'press', 'command + s', '--class', 'MotionEditor')
    check_saved()
    print('Canvas presets, validation, persistence, shared export controls and cancellation passed', flush=True)
finally:
    session.stop_app()
