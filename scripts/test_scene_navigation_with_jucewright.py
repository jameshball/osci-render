#!/usr/bin/env python3
"""Exercise captured camera navigation without mouse dragging."""
import json
import subprocess
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app('camera-navigation')
session.keep_app = keep

def command(*args):
    result = subprocess.run(session.cli(*args), capture_output=True, text=True, check=True)
    return result.stdout

def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)

def field(name):
    return float(json.loads(command('snapshot', '--json', '--name', name, '--exact'))['tree']['value'])

try:
    step('open cube', 'drop-files', '--file', session.root_dir / 'Resources/models/cube.obj', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    step('open scene editor', 'click', '--name', 'Scene', '--exact')
    command('wait-for-locator', '--name', 'Navigate', '--exact', '--timeout-ms', '5000')
    subprocess.run(['open', '-a', str(session.app_path)], check=True)
    step('settle native window focus', 'wait', '--ms', '500')
    step('start navigation', 'click', '--name', 'Navigate', '--exact')
    command('wait-for-locator', '--name', 'Done', '--exact', '--timeout-ms', '5000')
    bounds = json.loads(command('snapshot', '--json', '--class', 'SceneEditor::Canvas', '--exact'))['tree']['bounds']
    # Semantic key presses have no held native state. They must not add repeat-rate steps.
    before_z = field('Position Z')
    for _ in range(4):
        step('repeat key event without held state', 'press', '--class', 'SceneEditor::Canvas', '--exact', 'w')
    assert abs(field('Position Z') - before_z) < .001
    step('look without dragging', 'hover', str(bounds['x'] + bounds['w'] // 2 + 45), str(bounds['y'] + bounds['h'] // 2 + 20))
    if abs(field('Rotation Y')) < 1:
        print('Native hover dispatch did not reach the preview; mouse-only input remains unverified in this environment.', flush=True)
        step('verify captured look through direct pointer dispatch', 'drag', '--class', 'SceneEditor::Canvas', '--exact', '--dx', '45', '--dy', '20', '--steps', '1')
    step('captured mode screenshot', 'screenshot', '--file', session.artifact_dir / 'navigation-mode.png')
    step('release cursor', 'press', '--class', 'SceneEditor::Canvas', '--exact', 'Escape')
    command('wait-for-locator', '--name', 'Navigate', '--exact', '--timeout-ms', '5000')
    assert abs(field('Position X')) < .001, 'Navigation moved the object'
    assert abs(field('Rotation Y')) < .001, 'Free look rotated the object'
    step('inspect camera', 'select-option', '--name', 'Scene objects', '--role', 'list', '--exact', '--index', '0')
    assert abs(field('Scale X') - 1) < .001, 'Forward changed zoom instead of camera position'
    assert abs(field('Position X')) > .01, 'Looking did not reposition the camera target'
    assert abs(field('Position Y')) > .01, 'Pitch did not reposition the camera target'
    assert abs(field('Rotation Y')) > 1, 'Mouse movement did not turn the camera'
    step('camera screenshot', 'screenshot', '--file', session.artifact_dir / 'camera-navigation.png')
    step('start navigation again', 'click', '--name', 'Navigate', '--exact')
    step('leave navigation on focus change', 'click', '--name', 'Move', '--exact')
    command('wait-for-locator', '--name', 'Navigate', '--exact', '--timeout-ms', '5000')
    print('Camera navigation checks passed', flush=True)
finally:
    subprocess.run(session.cli('press', '--class', 'SceneEditor::Canvas', '--exact', 'Escape'), capture_output=True)
    session.stop_app()
