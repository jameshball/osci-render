#!/usr/bin/env python3
"""Exercise shared source editing for normal files, scene objects and Lua effects."""
import json
import subprocess
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app('editor-workspace')
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def tab(name):
    step(name, 'click', '--class', 'osci::TabBar::Tab', '--name', name, '--exact')


def source():
    return json.loads(command('snapshot', '--json', '--class', 'osci::LuaScriptEditorComponent::Editor', '--exact'))['tree']['value']


try:
    tab('Effects')
    step('open ordinary Lua file', 'drop-files', '--file', session.root_dir / 'Resources/lua/spiral.lua', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    tab('Editor')
    assert source()
    root = json.loads(command('snapshot', '--json', '--depth', '3'))['tree']
    source_bounds = next(child['bounds'] for child in root['children'] if child['class'] == 'osci::LuaScriptEditorComponent')
    tabs_bounds = json.loads(command('snapshot', '--json', '--class', 'osci::TabBar', '--exact'))['tree']['bounds']
    assert source_bounds['x'] == tabs_bounds['x'] and source_bounds['y'] == tabs_bounds['y'] + tabs_bounds['h'] + 3, 'Editor has a redundant outer container'
    command('wait-for-locator', '--class', 'LuaComponent', '--exact')
    step('Lua slider remains available', 'set-value', '--role', 'slider', '--name', 'Lua Slider A', '--exact', '0.25')
    step('reset Lua state', 'click', '--name', 'luaReset', '--exact')
    step('edit file in workspace', 'fill', '--class', 'osci::LuaScriptEditorComponent::Editor', '--exact', 'return { 0.2, 0.3, 0 }')
    command('wait', '--ms', '350')
    tab('Effects')
    tab('Editor')
    assert source() == 'return { 0.2, 0.3, 0 }'
    step('file Editor screenshot', 'screenshot', '--file', session.artifact_dir / 'file-editor.png')
    tab('Scene')
    step('pencil opens same Lua source', 'click', '--name', 'Edit spiral.lua', '--exact')
    assert source() == 'return { 0.2, 0.3, 0 }'
    tab('Scene')
    step('add text object', 'drop-files', '--file', session.root_dir / 'Resources/text/helloworld.txt', '--class', 'OscirenderAudioProcessorEditor', '--exact')
    step('object source context menu', 'right-click', '--class', 'SceneEditor::ObjectRow', '--name', 'helloworld.txt', '--exact')
    assert 'Expose to DAW' not in command('snapshot', '--json', '--class', 'SceneEditor::MenuPanel', '--exact'), 'Standalone context menu exposes DAW automation'
    step('context menu edit source', 'click', '--class', 'juce::TextButton', '--name', 'Edit source', '--exact')
    assert source() == 'hello\nworld'
    tab('Effects')
    # The effect grid follows the shared EFFECTS order.
    from jucewright_osci_browser.constants import EFFECTS
    step('add Lua effect', 'click', '--class', 'osci::GridItemComponent', '--nth', str(EFFECTS.index('Lua Effect')), '--exact')
    step('effect pencil opens Editor', 'click', '--class', 'osci::SvgButton', '--name', 'Lua Effect', '--exact')
    step('edit Lua effect', 'fill', '--class', 'osci::LuaScriptEditorComponent::Editor', '--exact', 'return { x * 0.5, y, z }')
    command('wait', '--ms', '350')
    tab('Scene')
    tab('Editor')
    assert source() == 'return { x * 0.5, y, z }', 'Effect target changed after visiting Scene'
    step('console remains accessible', 'click', '--name', 'pauseConsole', '--exact')
    step('effect Editor screenshot', 'screenshot', '--file', session.artifact_dir / 'effect-editor.png')
    tab('Effects')
    step('effect pencil remains a direct action', 'click', '--class', 'osci::SvgButton', '--name', 'Lua Effect', '--exact')
    assert source() == 'return { x * 0.5, y, z }'
    tab('Scene')
    step('object pencil retargets Editor', 'click', '--name', 'Edit helloworld.txt', '--exact')
    assert source() == 'hello\nworld'
    step('close the file while Editor is open', 'click', '--name', 'closeFile', '--exact')
    command('wait', '--ms', '200')
    assert 'LuaScriptEditorComponent::Editor' not in command('snapshot', '--json'), 'Closed source editor remained visible'
    print('File, scene and effect Editor checks passed', flush=True)
finally:
    session.stop_app()
