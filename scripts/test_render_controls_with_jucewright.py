#!/usr/bin/env python3
"""Require complete coverage of dynamically rebuilt Render visualiser controls."""
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.scenario import OsciRenderBrowserRun

run = OsciRenderBrowserRun(parse_args())
assert run.find_jucewright()
run.launch_app('fresh-control-references')
try:
    run.exercise_visualiser_settings_dialog()
    assert not run.failures, run.failures
    assert not run.optional_failures, run.optional_failures
    print('Dynamic visualiser controls passed without required or optional failures', flush=True)
finally:
    run.stop_app()
