#!/usr/bin/env python3
"""Run deterministic processor workloads serially; retain every result and failure."""
import argparse
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


def numbers(value, cast):
    return [cast(x) for x in value.split(',')]


def validate_measurement(measurement, rate, block, ratio, warmup, blocks, expect_nonzero=True):
    def finite_number(key):
        value = measurement.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError(f'Missing or nonfinite measurement: {key}')
        return value

    for key in ('mean_us', 'p50_us', 'p95_us', 'p99_us', 'max_us'):
        if finite_number(key) <= 0:
            raise ValueError(f'Nonpositive callback timing: {key}')
    for key in ('processor_construction_ms', 'project_restore_ms', 'voice_setup_wait_ms',
                'mean_deadline_fraction', 'p99_deadline_fraction', 'max_deadline_fraction', 'energy', 'peak'):
        if finite_number(key) < 0:
            raise ValueError(f'Negative measurement: {key}')
    for key, expected in (('sample_rate', rate), ('block_size', block), ('ratio', ratio),
                          ('warmup_blocks', warmup), ('measured_blocks', blocks)):
        if finite_number(key) != expected:
            raise ValueError(f'Measurement configuration mismatch: {key}')
    if finite_number('nonfinite_samples') != 0:
        raise ValueError('Nonfinite audio samples')
    finite_number('checksum')
    if expect_nonzero and measurement['energy'] <= 1e-12:
        raise ValueError('Insufficient audio energy')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--benchmark', type=Path, required=True)
    parser.add_argument('--compare-with', type=Path, help='Interleave a second executable, reversing pair order each repetition')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cases', default='', help='Comma-separated case indexes; defaults to all')
    parser.add_argument('--sample-rates', default='48000')
    parser.add_argument('--block-sizes', default='256')
    parser.add_argument('--ratios', default='1')
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--warmup', type=int, default=1000)
    parser.add_argument('--blocks', type=int, default=2000)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--ffmpeg', type=Path, help='Standalone FFmpeg binary to install in the isolated app profile')
    args = parser.parse_args()
    if args.repeat <= 0:
        parser.error('--repeat must be positive')
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = json.loads(args.manifest.read_text())['cases']
    if args.cases:
        cases = [cases[i] for i in numbers(args.cases, int)]
    skipped = [case for case in cases if sys.platform not in case.get('supported_platforms', [sys.platform])]
    for case in skipped:
        print(f"Skipping {case['id']}: requires platform {case['supported_platforms']}", flush=True)
    cases = [case for case in cases if case not in skipped]
    configurations = list(itertools.product(numbers(args.sample_rates, float), numbers(args.block_sizes, int), numbers(args.ratios, float)))
    if any(not math.isfinite(rate) or not math.isfinite(ratio) or rate <= 0 or block <= 0 or ratio <= 0
           for rate, block, ratio in configurations):
        parser.error('Sample rates, block sizes, and ratios must be finite and positive')
    configurations = [config for config in configurations if config[0] * config[2] <= 1e6]
    if not cases or not configurations:
        parser.error('No executable workload configurations')
    binary = args.benchmark.resolve()
    binaries = [('before', binary)]
    if args.compare_with:
        binaries.append(('after', args.compare_with.resolve()))
    metadata = {'binary': str(binary), 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'manifest_sha256': hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
                'skipped_cases': [{'id': case['id'], 'reason': 'unsupported platform', 'supported_platforms': case['supported_platforms']} for case in skipped],
                'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'arguments': vars(args).copy()}
    metadata['binaries'] = {label: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for label, path in binaries}
    metadata['arguments'] = {k: str(v) if isinstance(v, Path) else v for k, v in metadata['arguments'].items()}
    metadata['allocator_interposers'] = [{'path': path, 'sha256': hashlib.sha256(Path(path).read_bytes()).hexdigest()}
                                       for path in os.environ.get('DYLD_INSERT_LIBRARIES', '').split(':') if path]
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2))
    environment = os.environ.copy()
    home = args.output / 'home'
    home.mkdir(exist_ok=True)
    environment.update(HOME=str(home), CFFIXED_USER_HOME=str(home), JUCEWRIGHT_AUTOMATION='1')
    if args.ffmpeg:
        destination = home / 'Library/Application Support/osci-render/ffmpeg'
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.ffmpeg, destination)
    elif any('ffmpeg' in case.get('runtime_dependencies', []) for case in cases):
        parser.error('Video workloads require --ffmpeg to avoid a download prompt or fallback source')
    results = []
    # Interleave repeats rather than running all repetitions of one workload together.
    for repeat in range(args.repeat):
        for case, (rate, block, ratio), (variant, binary) in itertools.product(cases, configurations, binaries[::1 if repeat % 2 == 0 else -1]):
            name = f"{case['id']}-sr{rate:g}-b{block}-x{ratio:g}-r{repeat}"
            if args.compare_with:
                name += '-' + variant
            output = args.output / (name + '.json')
            command = [str(binary), '--project', str((args.manifest.parent / case['project']).resolve()),
                       '--sample-rate', str(rate), '--block-size', str(block), '--ratio', str(ratio),
                       '--voices', str(case['voices']), '--midi', case['midi']['mode'],
                       '--velocity', str(case['midi'].get('velocity', 80)),
                       '--input-amplitude', str(case.get('host_input_amplitude', 0.0)),
                       '--notes', ','.join(str(note) for note in case['midi']['notes']),
                       '--warmup', str(args.warmup), '--blocks', str(args.blocks), '--output', str(output)]
            output.unlink(missing_ok=True)
            started = time.monotonic()
            with output.with_suffix('.log').open('w') as log:
                try:
                    result = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
                    status = result.returncode
                except subprocess.TimeoutExpired:
                    status = 'timeout'
            row = {'case': case['id'], 'repeat': repeat, 'variant': variant, 'rate': rate, 'block': block, 'ratio': ratio,
                   'status': status, 'wall_seconds': time.monotonic() - started, 'output': output.name}
            if status == 0:
                try:
                    measurement = json.loads(output.read_text())
                    if not isinstance(measurement, dict):
                        raise ValueError('Measurement must be a JSON object')
                    validate_measurement(measurement, rate, block, ratio, args.warmup, args.blocks, case.get('expect_nonzero_audio', True))
                    row['measurement'] = measurement
                except FileNotFoundError:
                    row['status'] = 'missing-measurement'
                except (OSError, ValueError):
                    row['status'] = 'invalid-measurement'
            if 'measurement' in row:
                if row['measurement'].get('enabled_effects') != case['effects']:
                    row['status'] = 'effect-count-mismatch'
                assignments = row['measurement'].get('modulation_assignments', {})
                if (sum(assignments.values()) != case['modulation']
                        or (case['modulation'] > 0 and assignments.get(case['modulation_type'], 0) != case['modulation'])):
                    row['status'] = 'modulation-count-mismatch'
                if 'frequency_hz' in case and not math.isclose(row['measurement'].get('frequency_parameter_hz', 0), case['frequency_hz'], rel_tol=1e-5):
                    row['status'] = 'frequency-mismatch'
                expected_voices = min(case['voices'], len(case['midi']['notes'])) if case['midi']['mode'] != 'off' else 1
                if row['measurement'].get('max_active_voices', 0) < expected_voices:
                    row['status'] = 'insufficient-active-voices'
                if case.get('expect_nonzero_audio', True) and not row['measurement'].get('active_audio_verified', False):
                    row['status'] = 'inactive-audio'
                kind = case['source']['kind']
                expected_parser = {'3d': 'obj', '3d-heavy': 'obj', 'lua-heavy': 'lua',
                                   'jpeg': 'image', 'jpg': 'image', 'gif': 'image', 'mp4': 'image',
                                   'mov': 'image', 'flac': 'audio', 'aiff': 'audio', 'ogg': 'audio',
                                   'mp3': 'audio', 'aac': 'audio', 'm4a': 'audio', 'lottie-json': 'lottie', 'lottie-lot': 'lottie'}.get(kind, kind)
                if kind != 'defaults' and (row['measurement'].get('loaded_parser') != expected_parser
                                          or row['measurement'].get('selected_file') != case['source']['name']):
                    row['status'] = 'source-mismatch'
                if kind in ('gif', 'mp4', 'mov', 'lottie', 'lottie-json', 'lottie-lot') and row['measurement'].get('source_frames', 0) <= 1:
                    row['status'] = 'animation-not-loaded'
            results.append(row)
            (args.output / 'results.json').write_text(json.dumps(results, indent=2))
            measurement = row.get('measurement', {})
            print(name, 'status=', row['status'], 'mean_us=', measurement.get('mean_us'),
                  'audio=', measurement.get('active_audio_verified'), flush=True)
    return int(any(row['status'] != 0 for row in results))


if __name__ == '__main__':
    raise SystemExit(main())
