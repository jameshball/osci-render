#!/usr/bin/env python3
"""Measure full-length standalone playback; CPU is JUCE's smoothed callback load.

Run with one Motion instance. Device xruns are observed counter deltas, not an
allocation audit or proof that physical XYRGB hardware output is correct.
"""
import argparse
import json
import math
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

options = argparse.ArgumentParser(add_help=False)
options.add_argument('--project', type=Path, required=True)
options.add_argument('--seconds', type=float)
options.add_argument('--block-size', type=int, default=256)
options.add_argument('--sample-rate', type=float, default=48000)
args, remainder = options.parse_known_args()
sys.argv = [sys.argv[0]] + remainder
assert args.block_size > 0 and math.isfinite(args.sample_rate) and args.sample_rate > 0


class PlaybackSession(BrowserSession):
    def prepare_profile(self, launch_home):
        profile = super().prepare_profile(launch_home)
        settings = Path(profile['settingsFile'])
        assert settings.resolve().is_relative_to(launch_home.resolve())
        tree = ET.parse(settings)
        setup = tree.find("./VALUE[@name='audioSetup']/DEVICESETUP")
        assert setup is not None
        setup.set('audioDeviceBufferSize', str(args.block_size))
        setup.set('audioDeviceRate', str(args.sample_rate))
        tree.write(settings, encoding='UTF-8', xml_declaration=True)
        return profile


session = PlaybackSession(parse_args())
raw = args.project.read_bytes()
root = ET.fromstring(raw[8:8 + struct.unpack('<I', raw[4:8])[0]])
composition = root.find('composition')
duration = float(composition.get('duration'))
run_seconds = args.seconds if args.seconds is not None else duration + 3
assert math.isfinite(run_seconds) and run_seconds > 0
assert session.find_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app('playback-measurement')
session.keep_app = keep


def command(*args):
    return subprocess.check_output(session.cli(*args), text=True)


def step(label, *args):
    assert session.run_step(label, session.cli(*args)), label


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def value(name):
    visibility = ['--hidden'] if name == 'Playback diagnostics' else []
    tree = json.loads(command('snapshot', '--component-name', name, *visibility, '--json', '--full'))
    node = next(node for node in nodes(tree) if node.get('componentName') == name)
    return node.get('value', node.get('text', ''))


samples = []
try:
    subprocess.run(['open', '-a', str(session.app_path), str(args.project.resolve())], check=True)
    command('wait-for-value', '--component-name', 'Composition name', '--hidden', '--value', composition.get('name'), '--timeout-ms', 120000)
    step('normal workspace', 'resize-window', '--w', 1440, '--h', 900)
    # The project is opened without saving; the source artifact stays unchanged.
    step('seek start', 'fill', '--component-name', 'Timeline position', '--class', 'juce::Label', '0s')
    command('wait-for-value', '--component-name', 'Timeline position', '--value', '0.000s')
    time.sleep(2)
    baseline = json.loads(value('Playback diagnostics'))
    ready_deadline = time.monotonic() + 30
    while baseline.get('preparing', True) and time.monotonic() < ready_deadline:
        time.sleep(.5)
        baseline = json.loads(value('Playback diagnostics'))
    assert not baseline.get('preparing', True), 'Composition preparation did not finish'
    assert baseline['available']
    assert baseline['sample_rate'] == args.sample_rate and baseline['block_size'] == args.block_size, 'Device did not accept requested sample rate/buffer'
    step('play benchmark', 'click', '--name', 'Play', '--class', 'juce::TextButton', '--exact')
    started = time.monotonic()
    next_report = 30
    while time.monotonic() - started < run_seconds:
        time.sleep(min(2, max(0, run_seconds - (time.monotonic() - started))))
        sample = json.loads(value('Playback diagnostics'))
        sample['wall_seconds'] = time.monotonic() - started
        position_text = value('Timeline position')
        assert position_text.endswith('s'), position_text
        sample['position'] = float(position_text[:-1])
        delta = abs(sample['position'] - sample['wall_seconds'] % duration)
        sample['transport_wall_error_seconds'] = min(delta, duration - delta)
        assert sample['available'] and math.isfinite(sample['cpu_load'])
        assert (sample['device'], sample['sample_rate'], sample['block_size']) == (baseline['device'], baseline['sample_rate'], baseline['block_size']), 'Device changed during measurement'
        assert sample['observed_ms'] > (samples[-1]['observed_ms'] if samples else baseline['observed_ms']), 'Stale device observation'
        samples.append(sample)
        if sample['wall_seconds'] >= next_report:
            print(f"Playback {sample['wall_seconds']:.1f}s; position {sample['position']:.3f}s; audio load {sample['cpu_load'] * 100:.1f}%; xruns +{sample['xruns'] - baseline['xruns']}", flush=True)
            next_report += 30
    step('pause benchmark', 'click', '--name', 'Pause', '--class', 'juce::TextButton', '--exact')
    step('playback screenshot', 'screenshot', '--file', session.artifact_dir / 'playback.png')
    step('playback health details', 'click', '--component-name', 'Playback health', '--exact')
    command('wait', '--ms', 500)
    step('health screenshot', 'screenshot', '--file', session.artifact_dir / 'playback-health.png')
    step('close health details', 'press', 'Escape')
    loads = sorted(sample['cpu_load'] for sample in samples)
    expected_last = samples[-1]['wall_seconds'] % duration
    error = abs(samples[-1]['position'] - expected_last)
    error = min(error, duration - error)
    report = {
        'project': str(args.project.resolve()), 'duration': duration,
        'tracks': len(composition.findall('track')), 'wall_seconds': samples[-1]['wall_seconds'],
        'baseline': baseline, 'samples': samples,
        'mean_observed_load': statistics.mean(loads), 'p95_observed_load': loads[math.ceil(len(loads) * .95) - 1],
        'max_observed_load': max(loads), 'xrun_delta': samples[-1]['xruns'] - baseline['xruns'],
        'transport_wall_error_seconds': error,
        'max_transport_wall_error_seconds': max(sample['transport_wall_error_seconds'] for sample in samples),
        'scope': 'Live device and visualiser; smoothed load observations, not per-callback percentiles or heap instrumentation.'
    }
    (session.artifact_dir / 'playback-report.json').write_text(json.dumps(report, indent=2))
    assert report['xrun_delta'] >= 0, 'Device counters reset during measurement'
    assert report['max_transport_wall_error_seconds'] < 1.0, f'Transport drift/stall: {report["max_transport_wall_error_seconds"]}s'
    print(json.dumps({key: report[key] for key in ('mean_observed_load', 'p95_observed_load', 'max_observed_load', 'xrun_delta', 'transport_wall_error_seconds')}), flush=True)
finally:
    session.stop_app()
