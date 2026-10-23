#!/usr/bin/env python3
"""Regression coverage for native visualiser layers in component screenshots."""
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession
from jucewright_osci_browser.png import check_png_not_blank

session = BrowserSession(parse_args())
if not session.find_jucewright():
    session.build_jucewright()
keep_app = session.keep_app
session.keep_app = False
session.launch_app('native-screenshot-tests')
session.keep_app = keep_app

def step(name, *args):
    if not session.run_step(name, session.cli(*args)):
        raise RuntimeError(name)

def capture(label):
    for source in ['component', 'auto']:
        filename = session.artifact_dir / f'{label}-{source}.png'
        step(f'{label} {source} screenshot', 'screenshot', '--class', 'VisualiserComponent', '--exact', '--source', source, '--file', filename)
        check_png_not_blank(filename, crop_bottom_fraction=0.12)
    step(f'{label} root screenshot', 'screenshot', '--target', 'root', '--file', session.artifact_dir / f'{label}-root.png')

try:
    step('disable MIDI', 'set-checked', '--class', 'jux::SwitchButton', '--nth', '0', 'false')
    step('open cube', 'drop-files', '--file', session.root_dir / 'Resources/models/cube.obj', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    step('allow first presented frame', 'wait', '--ms', '1200')
    capture('cube')
    step('open scene editor', 'click', '--name', 'Scene', '--exact')
    step('wait for editor', 'wait-for-locator', '--name', 'Navigate', '--exact', '--timeout-ms', '5000')
    step('rotate cube', 'set-value', '--name', 'Rotation Y', '--exact', '30')
    step('add text through main import', 'drop-files', '--file', session.root_dir / 'Resources/text/helloworld.txt', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    step('position text', 'set-value', '--name', 'Position Y', '--exact', '-0.8')
    for axis in 'XYZ':
        step('scale text ' + axis, 'set-value', '--name', 'Scale ' + axis, '--exact', '0.35')
    step('capture scene panel', 'screenshot', '--file', session.artifact_dir / 'scene-panel.png')
    step('close scene editor', 'click', '--name', 'Effects', '--exact')
    step('allow resumed presentation', 'wait', '--ms', '1200')
    capture('scene')
    step('resize window', 'resize-window', '--w', '900', '--h', '650')
    step('allow resized presentation', 'wait', '--ms', '800')
    capture('resized')
    print('Native presentation screenshot checks passed', flush=True)
finally:
    session.stop_app()
