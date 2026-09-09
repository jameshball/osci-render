#!/usr/bin/env python3
"""Check the integrated scene panel, shared imports, and inline source editing."""
import json
import subprocess
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app('scene-panel')
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def tree(class_name):
    return json.loads(command('snapshot', '--json', '--full', '--class', class_name, '--exact'))['tree']


def click(name):
    args = ['click', '--name', name, '--exact']
    if name in ('Effects', 'Scene', 'Editor', 'Examples'):
        args += ['--class', 'osci::TabBar::Tab']
    step(name, *args)


try:
    step('keyboard skips unavailable Scene tab', 'press', '--name', 'Effects', '--exact', 'Left')
    step('arrow tab focus screenshot', 'screenshot', '--file', session.artifact_dir / 'scene-tab-arrow-focus.png')
    command('wait-for-locator', '--class', 'OpenFileComponent', '--exact')
    step('Left skips unavailable Scene tab', 'press', '--name', 'Examples', '--exact', 'Right')
    command('wait-for-locator', '--class', 'EffectsComponent', '--exact')
    step('open cube', 'drop-files', '--file', session.root_dir / 'Resources/models/cube.obj', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    click('Scene')
    step('select camera', 'select-option', '--name', 'Scene objects', '--role', 'list', '--exact', '--index', '0')
    step('standalone camera context menu', 'right-click', '--class', 'SceneEditor::ObjectRow', '--name', 'Camera', '--exact')
    assert 'SceneEditor::MenuPanel' not in command('snapshot', '--json'), 'Standalone camera offers an empty DAW-only menu'
    step('select cube', 'select-option', '--name', 'Scene objects', '--role', 'list', '--exact', '--index', '1')
    preview = tree('SceneEditor::Canvas')['bounds']
    visualiser = tree('VisualiserComponent')['bounds']
    assert visualiser['x'] + visualiser['w'] < preview['x'], 'Scene obscures output'
    assert preview['h'] > 200
    headers = [child['bounds'] for child in tree('SceneEditor')['children'] if child['class'] == 'osci::PanelHeader']
    assert len(headers) == 3 and len({(h['y'], h['h']) for h in headers}) == 1 and headers[0]['h'] == 30, 'Scene headers are not aligned'
    ordered = sorted(headers, key=lambda h: h['x'])
    assert all(right['x'] - (left['x'] + left['w']) == 3 for left, right in zip(ordered, ordered[1:])), 'Scene panels have oversized gaps'
    for axis in 'XYZ':
        step('size first cube ' + axis, 'set-value', '--name', 'Scale ' + axis, '--exact', '0.6')
    step('place first cube', 'set-value', '--name', 'Position X', '--exact', '-0.45')
    step('turn first cube', 'set-value', '--name', 'Rotation Y', '--exact', '25')
    click('Examples')
    step('existing example adds to scene', 'click', '--name', 'cube.obj', '--class', 'osci::GridItemComponent', '--exact')
    command('wait-for-locator', '--class', 'SceneEditor', '--exact')
    for axis in 'XYZ':
        step('size second cube ' + axis, 'set-value', '--name', 'Scale ' + axis, '--exact', '0.5')
    step('place second cube', 'set-value', '--name', 'Position X', '--exact', '0.5')
    step('turn second cube', 'set-value', '--name', 'Rotation Y', '--exact', '-25')
    step('existing root import adds text', 'drop-files', '--file', session.root_dir / 'Resources/text/helloworld.txt', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    step('position text', 'set-value', '--name', 'Position Y', '--exact', '-0.55')
    step('scale text', 'set-value', '--name', 'Scale X', '--exact', '0.4')
    step('scale text vertically', 'set-value', '--name', 'Scale Y', '--exact', '0.4')
    click('Edit helloworld.txt')
    tree('osci::LuaScriptEditorComponent')
    step('edit source inline', 'fill', '--class', 'osci::LuaScriptEditorComponent::Editor', '--exact', 'osci-render')
    step('source editor screenshot', 'screenshot', '--file', session.artifact_dir / 'scene-source-editor.png')
    click('Scene')
    click('Editor')
    assert tree('osci::LuaScriptEditorComponent::Editor')['value'] == 'osci-render', 'Source edit was not retained'
    click('Scene')
    assert 'Expose to DAW' not in command('snapshot', '--json', '--class', 'SceneEditor', '--exact'), 'Standalone exposes DAW-only controls'
    step('rename selected object', 'fill', '--name', 'helloworld.txt', '--role', 'editableText', '--exact', 'title.txt')
    step('commit selected object name', 'press', '--name', 'title.txt', '--role', 'editableText', '--exact', 'Enter')
    command('wait-for-locator', '--name', 'title.txt', '--role', 'listItem', '--exact')
    step('open object menu', 'right-click', '--name', 'title.txt', '--role', 'listItem', '--exact')
    assert 'SceneEditor::MenuPanel' not in command('snapshot', '--json'), 'Scene used its old one-off menu panel'
    step('object menu screenshot', 'screenshot', '--source', 'native', '--file', session.artifact_dir / 'scene-object-menu.png')
    step('dismiss object menu', 'press', '--class', 'OscirenderAudioProcessorEditor', '--exact', 'Escape')
    step('select second cube for keyboard deletion', 'select-option', '--name', 'Scene objects', '--role', 'list', '--exact', '--index', '2')
    step('delete selected object from keyboard', 'press', '--name', 'Scene objects', '--role', 'list', '--exact', 'Delete')
    assert 'Undo Remove scene object' in command('snapshot', '--json', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    step('undo keyboard object deletion', 'click', '--name', 'Undo', '--exact')
    step('add object button opens shared importer', 'click', '--name', '+ Add object', '--exact')
    command('wait-for-locator', '--class', 'OpenFileComponent', '--exact')
    click('Scene')
    step('scene panel screenshot', 'screenshot', '--file', session.artifact_dir / 'scene-panel.png')
    step('add Lua source to scene', 'drop-files', '--file', session.root_dir / 'Resources/lua/hypercube.lua', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    step('Lua scene preview screenshot', 'screenshot', '--file', session.artifact_dir / 'scene-lua-preview.png')
    step('compact window', 'resize-window', '--w', '900', '--h', '650')
    compact_headers = [child['bounds'] for child in tree('SceneEditor')['children'] if child['class'] == 'osci::PanelHeader']
    assert len({(h['y'], h['h']) for h in compact_headers}) == 1, 'Compact toolbar wrapped out of alignment'
    step('compact screenshot', 'screenshot', '--file', session.artifact_dir / 'scene-panel-compact.png')
    step('restore window', 'resize-window', '--w', '1100', '--h', '770')
    click('Examples')
    step('import browser screenshot', 'screenshot', '--file', session.artifact_dir / 'scene-import-browser.png')
    click('Blender input')
    command('wait-for-locator', '--class', 'SceneEditor', '--exact')
    click('Effects')
    step('normal import creates a new scene', 'drop-files', '--file', session.root_dir / 'Resources/models/diamond.obj', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    click('Scene')
    step('new scene screenshot', 'screenshot', '--file', session.artifact_dir / 'scene-new-file.png')
    click('closeFile')
    command('wait-for-locator', '--class', 'SceneEditor', '--exact')
    click('closeFile')
    command('wait-for-locator', '--name', 'Delete scene?', '--role', 'dialogWindow', '--exact')
    step('multi-object delete confirmation screenshot', 'screenshot', '--file', session.artifact_dir / 'scene-delete-confirmation.png')
    step('confirm multi-object scene deletion', 'click', '--name', 'Delete scene', '--exact')
    command('wait', '--ms', '200')
    assert 'SceneEditor' not in command('snapshot', '--json', '--class', 'SettingsComponent', '--exact'), 'Closing the last scene left its editor open'
    step('undo scene deletion', 'click', '--name', 'Undo', '--exact')
    click('Scene')
    command('wait-for-locator', '--class', 'SceneEditor', '--exact')
    step('redo scene deletion', 'click', '--name', 'Redo', '--exact')
    command('wait', '--ms', '200')
    assert 'SceneEditor' not in command('snapshot', '--json', '--class', 'SettingsComponent', '--exact'), 'Redo did not delete the restored scene'
    print('Integrated scene panel checks passed', flush=True)
finally:
    session.stop_app()
