#!/usr/bin/env python3
"""Check sosci's standalone XY playback and fullscreen after shared-code changes."""
import json
import math
import struct
import subprocess
import wave
from PIL import Image
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
assert session.find_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app('sosci-playback')
session.keep_app = keep


def step(label, *args):
    assert session.run_step(label, session.cli(*args)), label


def find(node, class_name):
    if isinstance(node, dict):
        if node.get('class') == class_name:
            return node
        for child in node.values():
            found = find(child, class_name)
            if found is not None:
                return found
    elif isinstance(node, list):
        for child in node:
            found = find(child, class_name)
            if found is not None:
                return found
    return None


try:
    step('sosci editor', 'wait-for-locator', '--class', 'SosciPluginEditor', '--exact')
    step('size workspace', 'resize-window', '--w', 1180, '--h', 750)
    fixture = session.artifact_dir / 'XY circle.wav'
    with wave.open(str(fixture), 'wb') as audio:
        audio.setnchannels(2)
        audio.setsampwidth(2)
        audio.setframerate(48000)
        audio.writeframes(b''.join(struct.pack('<hh', round(16000 * math.sin(2 * math.pi * 220 * i / 48000)),
                                             round(16000 * math.cos(2 * math.pi * 220 * i / 48000))) for i in range(48000 * 8)))
    step('load XY audio', 'drop-files', '--file', fixture, '--class', 'SosciPluginEditor', '--exact')
    step('settle beam', 'wait', '--ms', 800)
    step('playback screenshot', 'screenshot', '--file', session.artifact_dir / 'sosci-playback.png')
    tree = json.loads(subprocess.check_output(session.cli('snapshot', '--json', '--full'), text=True))
    assert find(tree, 'MotionEditor') is None
    bounds = find(tree, 'VisualiserComponent')['bounds']
    image = Image.open(session.artifact_dir / 'sosci-playback.png').convert('RGB')
    x, y, w, h = (bounds[key] for key in ('x', 'y', 'w', 'h'))
    region = image.crop((x + w // 10, y + h // 10, x + w * 9 // 10, y + h * 8 // 10))
    green = sum(1 for r, g, b in region.getdata() if g > 60 and g > r * 1.3 and g > b * 1.15)
    assert green > 100, f'Expected visible XY beam; got {green} green pixels'
    step('fullscreen', 'click', '--name', 'fullScreen', '--exact')
    step('fullscreen screenshot', 'screenshot', '--file', session.artifact_dir / 'sosci-fullscreen.png')
    step('return to workspace', 'click', '--name', 'fullScreen', '--exact')
    step('restored editor', 'wait-for-locator', '--class', 'SosciPluginEditor', '--exact')
    print(f'Sosci XY playback and fullscreen passed; {green} beam-coloured pixels', flush=True)
finally:
    session.stop_app()
