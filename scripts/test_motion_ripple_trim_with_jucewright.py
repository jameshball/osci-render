#!/usr/bin/env python3
"""Exercise per-track ripple edge trimming, cancellation, locking and persisted timing."""
import json
import math
import struct
import subprocess
import xml.etree.ElementTree as ET
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
assert session.find_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app('ripple-trim')
session.keep_app = keep


def command(*args):
    return subprocess.check_output(session.cli(*args), text=True)


def step(label, *args):
    assert session.run_step(label, session.cli(*args)), label


def encoded(data):
    alphabet = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'
    value = int.from_bytes(data, 'little')
    return str(len(data)) + '.' + ''.join(alphabet[(value >> bit) & 63] for bit in range(0, len(data) * 8, 6))


root = ET.Element('motion-project', schema='1')
composition = ET.SubElement(root, 'composition', name='Ripple trim study', duration='30', bpm='120', fps='30')
asset = ET.SubElement(composition, 'asset', id='1', name='Triangle.obj', extension='.obj')
asset.text = encoded(b'v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0 0.5 0\nf 1 2 3\n')
for track_id, title, clips in [(2, 'Edited sequence', [(3, 0, 5), (4, 5, 5), (6, 12, 5)]),
                              (20, 'Independent layer', [(21, 5, 5)]), (30, 'Locked layer', [(31, 5, 5)])]:
    track = ET.SubElement(composition, 'track', id=str(track_id), name=title, kind='visual', locked='1' if track_id == 30 else '0')
    for clip_id, start, duration in clips:
        beats = clip_id == 6
        clip = ET.SubElement(track, 'clip', id=str(clip_id), asset='1', name=f'Clip {clip_id}',
                             start=str(start * (2 if beats else 1)), duration=str(duration * (2 if beats else 1)),
                             offset='0.5', rate='1.25', timeBase='beats' if beats else 'seconds', contentBpm='120')
        for group, base in [('position', 0), ('rotation', 0), ('scale', 1)]:
            for axis in 'xyz':
                ET.SubElement(clip, 'property', name=group + '.' + axis, base=str(base))
        for name, base in [('red', .2), ('green', 1), ('blue', .35), ('weight', 1)]:
            ET.SubElement(clip, 'property', name=name, base=str(base))
ET.SubElement(composition, 'camera', id='5', name='Camera')
ET.SubElement(composition, 'marker', id='40', time='8', name='Fixed music cue')
xml = ET.tostring(root, encoding='utf-8')
project = session.artifact_dir / 'ripple-trim.osci-motion'
project.write_bytes(struct.pack('<II', 0x21324356, len(xml)) + xml + b'\0')


def saved():
    step('save timing', 'press', 'command + s', '--class', 'MotionEditor')
    data = project.read_bytes()
    value = ET.fromstring(data[8:8 + struct.unpack('<I', data[4:8])[0]])
    return value, {c.get('id'): c for c in value.findall('./composition/track/clip')}


def values(clip):
    scale = .5 if clip.get('timeBase') == 'beats' else 1
    return [float(clip.get('start')) * scale, float(clip.get('duration')) * scale,
            float(clip.get('offset')) * scale, float(clip.get('rate'))]


def assert_timing(expected):
    state, clips = saved()
    for id_, timing in expected.items():
        assert all(math.isclose(a, b, abs_tol=1e-9) for a, b in zip(values(clips[id_]), timing)), (id_, values(clips[id_]), timing)
    assert ET.tostring(state.find("./composition/track[@id='20']")) == independent
    assert ET.tostring(state.find("./composition/track[@id='30']")) == locked
    assert float(state.find('./composition/marker').get('time')) == 8
    assert float(state.find('./composition').get('duration')) == 30
    return state, clips


def area():
    tree = json.loads(command('snapshot', '--class', 'MotionTimelineView', '--exact', '--json', '--full'))
    return tree['tree']['bounds']


def drag(label, x, dx, row=0):
    bounds = area()
    y = bounds['y'] + 68 + row * 40
    step(label, 'drag-xy', bounds['x'] + x, y, bounds['x'] + x + dx, y, '--steps', 8)


try:
    subprocess.run(['open', '-a', str(session.app_path), str(project)], check=True)
    command('wait-for-value', '--component-name', 'Composition name', '--hidden', '--value', 'Ripple trim study')
    step('normal workspace', 'resize-window', '--w', 1440, '--h', 900)
    state, clips = saved()
    independent = ET.tostring(state.find("./composition/track[@id='20']"))
    locked = ET.tostring(state.find("./composition/track[@id='30']"))
    baseline = {key: values(value) for key, value in clips.items()}
    step('editing tool menu', 'click', '--class', 'MotionTimelineView', '--position', '60,12')
    step('choose ripple trim', 'click', '--name', 'Ripple trim edges on this track (B)', '--role', 'menuItem', '--exact')
    drag('extend trailing edge two seconds', 517, 140)
    assert_timing({'3': [0, 7, .5, 1.25], '4': [7, 5, .5, 1.25], '6': [14, 5, .25, 1.25]})
    step('undo whole ripple gesture', 'click', '--name', 'Undo', '--exact')
    assert_timing(baseline)
    drag('trim leading content two seconds', 173, 140)
    leading = {'3': [0, 3, 3, 1.25], '4': [3, 5, .5, 1.25], '6': [10, 5, .25, 1.25]}
    assert_timing(leading)
    step('undo leading trim', 'click', '--name', 'Undo', '--exact')
    assert_timing(baseline)
    step('redo leading trim', 'click', '--name', 'Redo', '--exact')
    assert_timing(leading)
    drag('extend leading content one second', 173, -70)
    extended = {'3': [0, 4, 1.75, 1.25], '4': [4, 5, .5, 1.25], '6': [11, 5, .25, 1.25]}
    assert_timing(extended)
    bounds = area()
    step('begin cancellable trim', 'mouse-down', bounds['x'] + 447, bounds['y'] + 68)
    step('preview cancellable trim', 'mouse-move', bounds['x'] + 517, bounds['y'] + 68)
    step('cancel ripple gesture', 'press', 'Escape', '--class', 'MotionTimelineView')
    step('release cancelled gesture', 'mouse-up', bounds['x'] + 517, bounds['y'] + 68)
    assert_timing(extended)
    drag('locked layer cannot ripple', 867, 70, 2)
    assert_timing(extended)
    step('ordinary trim tool', 'press', 'V', '--class', 'MotionTimelineView')
    drag('ordinary trim leaves following clips in place', 447, -70)
    assert_timing({'3': [0, 3, 1.75, 1.25], '4': [4, 5, .5, 1.25], '6': [11, 5, .25, 1.25]})
    step('undo ordinary trim', 'click', '--name', 'Undo', '--exact')
    assert_timing(extended)
    step('ripple keyboard shortcut', 'press', 'B', '--class', 'MotionTimelineView')
    step('select ripple clip', 'click', '--class', 'MotionTimelineView', '--position', '250,68')
    step('ripple result screenshot', 'screenshot', '--file', session.artifact_dir / 'ripple-trim.png')
    step('compact workspace', 'resize-window', '--w', 1100, '--h', 740)
    step('compact ripple screenshot', 'screenshot', '--file', session.artifact_dir / 'ripple-trim-compact.png')
    session.keep_app = False
    session.stop_app()
    session.launch_app('ripple-trim-reopened')
    session.keep_app = keep
    subprocess.run(['open', '-a', str(session.app_path), str(project)], check=True)
    command('wait-for-value', '--component-name', 'Composition name', '--hidden', '--value', 'Ripple trim study')
    assert_timing(extended)
    print('Ripple trim both edges, mixed clocks, gaps, independent/locked tracks, cancellation, undo/redo and reopen passed.', flush=True)
finally:
    session.stop_app()
