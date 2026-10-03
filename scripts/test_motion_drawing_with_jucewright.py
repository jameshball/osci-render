#!/usr/bin/env python3
"""Draw a source with the pen, rectangle and freehand tools, undo and redo,
add it, then reopen it with Edit drawing and check the saved SVG."""
import json
import struct
import subprocess
import time
import xml.etree.ElementTree as ET
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
assert session.find_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app('drawing')
session.keep_app = keep


def command(*args):
    return subprocess.check_output(session.cli(*args), text=True)


def step(label, *args):
    assert session.run_step(label, session.cli(*args)), label


ALPHABET = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'


def decoded(text):
    size, chars = text.split('.', 1)
    value = 0
    for index, char in enumerate(chars):
        value |= ALPHABET.index(char) << (index * 6)
    return value.to_bytes(int(size), 'little')


root = ET.Element('motion-project', schema='1')
ET.SubElement(root, 'composition', name='Drawing study', duration='10', bpm='120', fps='30')
xml = ET.tostring(root, encoding='utf-8')
project = session.artifact_dir / 'drawing.osci-motion'
project.write_bytes(struct.pack('<II', 0x21324356, len(xml)) + xml + b'\0')


def saved():
    step('save project', 'press', 'command + s', '--class', 'MotionEditor')
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack('<I', data[4:8])[0]])


def canvas():
    """The editor's drawing square, as the editor lays it out."""
    bounds = json.loads(command('snapshot', '--class', 'MotionDrawingEditor', '--exact', '--json'))['tree']['bounds']
    width, height = bounds['w'] - 16, bounds['h'] - 76
    side = min(width - 44, height)
    left = bounds['x'] + 8 + (width - side - 44) // 2 + 44
    top = bounds['y'] + 8 + (height - side) // 2
    scale = side / 2.3
    return lambda u, v: (round(left + side / 2 + u * scale), round(top + side / 2 - v * scale))


def click(at, label):
    step(label + ' down', 'mouse-down', *at)
    step(label + ' up', 'mouse-up', *at)


def drag(points, label):
    step(label + ' down', 'mouse-down', *points[0])
    for point in points[1:]:
        step(label + ' move', 'mouse-move', *point)
    step(label + ' up', 'mouse-up', *points[-1])


def drawing_svg(state):
    assets = state.findall('./composition/asset')
    assert len(assets) == 1, len(assets)
    assert assets[0].get('extension') == '.svg'
    return decoded(assets[0].text).decode()


try:
    session.open_project(project)
    command('wait-for-value', '--component-name', 'Composition name', '--hidden', '--value', 'Drawing study')
    step('normal workspace', 'resize-window', '--w', 1440, '--h', 900)
    step('add source menu', 'click', '--name', 'Add source', '--exact')
    step('draw a shape', 'click', '--name', 'Draw a shape...', '--role', 'menuItem', '--exact')
    command('wait-for-locator', '--class', 'MotionDrawingEditor', '--exact')
    at = canvas()
    # Pen: a sharp point, a dragged curve point, a sharp point, then close.
    click(at(-.6, .2), 'pen first point')
    drag([at(0, .6), at(.15, .6), at(.3, .6)], 'pen curve point')
    click(at(.6, .2), 'pen third point')
    click(at(0, -.4), 'pen fourth point')
    click(at(-.6, .2), 'pen close on first point')
    step('rectangle tool', 'press', 'R', '--class', 'MotionDrawingEditor')
    drag([at(-.9, -.6), at(-.7, -.7), at(-.5, -.9)], 'rectangle')
    step('freehand tool', 'press', 'F', '--class', 'MotionDrawingEditor')
    drag([at(.3, -.6), at(.4, -.75), at(.5, -.6), at(.6, -.8), at(.7, -.6)], 'freehand')
    step('undo freehand', 'press', 'command + z', '--class', 'MotionDrawingEditor')
    step('redo freehand', 'press', 'command + shift + z', '--class', 'MotionDrawingEditor')
    step('drawing screenshot', 'screenshot', '--file', session.artifact_dir / 'drawing.png')
    step('name drawing', 'fill', '--name', 'Drawing name', '--class', 'juce::TextEditor', '--exact', 'Kite')
    step('add drawing', 'click', '--name', 'Add', '--class', 'juce::TextButton', '--exact')
    time.sleep(3)
    state = saved()
    svg = drawing_svg(state)
    paths = ET.fromstring(svg).findall('{http://www.w3.org/2000/svg}path')
    assert 'data-osci-motion-drawing' in svg and len(paths) == 3, svg
    assert paths[0].get('d').endswith('Z') and ' C' in paths[0].get('d'), paths[0].get('d')
    assert len(state.findall('./composition/track/clip')) == 1
    # Edit it again from the source's menu: drag the curve point down.
    step('source menu', 'click', '--name', 'Kite.svg', '--role', 'listItem', '--exact', '--button', 'right')
    step('edit drawing', 'click', '--name', 'Edit drawing...', '--role', 'menuItem', '--exact')
    command('wait-for-locator', '--class', 'MotionDrawingEditor', '--exact')
    at = canvas()
    step('select tool', 'press', 'V', '--class', 'MotionDrawingEditor')
    drag([at(0, .6), at(0, .4), at(0, .3)], 'move curve point')
    step('edited drawing screenshot', 'screenshot', '--file', session.artifact_dir / 'drawing-edit.png')
    step('save drawing', 'click', '--name', 'Save', '--class', 'juce::TextButton', '--exact')
    time.sleep(3)
    state = saved()
    edited = drawing_svg(state)
    assert edited != svg, 'the edit was saved'
    assert len(state.findall('./composition/track/clip')) == 1, 'editing replaces the source in place'
    print('Pen curves and closing, rectangle, freehand, undo/redo, adding and re-editing a drawing passed.', flush=True)
finally:
    session.stop_app()
