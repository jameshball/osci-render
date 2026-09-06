#!/usr/bin/env python3
"""Measure paced Jucewright message-thread requests against an existing session.

This does not measure visible frames or native live-resize presentation. Prepare
an isolated, input-disabled profile through the existing UI runner, for example:
  python3 scripts/browse_osci_render_with_jucewright.py --quick --keep-app --session ui-measure --app /path/to/osci-render.app
That command runs UI coverage and leaves its session open; it is not part of the
measurement. Set the desired muted/project/paused state before recording. Do not
bypass the runner's disable_profile_audio_input preparation when launching.
Resize is programmatic and restores the explicitly supplied base size on exit.
No screenshots, per-event subprocesses, app launches, or focus changes are used.
"""

import argparse
import json
import math
import os
from pathlib import Path
import socket
import time


class Client:
    def __init__(self, session, sessions_dir):
        matches = list(sessions_dir.glob('*-' + session + '.json'))
        if len(matches) != 1:
            raise ValueError(f'Expected one advertisement for session {session}; found {len(matches)}')
        self.config = json.loads(matches[0].read_text())
        self.sequence = 0

    def request(self, method, params=None):
        self.sequence += 1
        params = params or {}
        payload = {'id': self.sequence, 'token': self.config['token'], 'method': method, 'params': params}
        # The endpoint handles one request per connection. Keep this Python
        # process alive, but reconnect for each request as the protocol requires.
        with socket.create_connection((self.config['host'], self.config['port']), timeout=10) as connection:
            connection.sendall((json.dumps(payload) + '\n').encode())
            with connection.makefile() as reader:
                response = json.loads(reader.readline())
        if not response.get('ok'):
            raise RuntimeError(f'Jucewright {method} failed: {response.get("error", "unknown error")}')
        result = response['result']
        if params.get('snapshot') is False and result != {'performed': True}:
            raise RuntimeError('Expected lightweight acknowledgment; rebuild with snapshot:false support')
        return result


def positive_float(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('must be finite and positive')
    return value


def nonnegative_int(text):
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError('must be nonnegative')
    return value


def measure(client, args):
    samples = []
    started = time.monotonic()
    due = started
    try:
        while time.monotonic() - started < args.seconds:
            now = time.monotonic()
            if now < due:
                time.sleep(due - now)
            begin = time.monotonic()
            elapsed = begin - started
            if args.mode == 'resize':
                client.request('resize_window', {
                    'target': args.window,
                    'w': args.size[0] + int(args.amplitude[0] * math.sin(elapsed * 2)),
                    'h': args.size[1] + int(args.amplitude[1] * math.sin(elapsed * 2)),
                    'snapshot': False,
                })
            else:
                client.request('ping')
            end = time.monotonic()
            sample = {'at_seconds': elapsed, 'rtt_ms': (end - begin) * 1000,
                      'schedule_lateness_ms': max(0, begin - due) * 1000}
            if args.settings is not None:
                sample['settings_mtime_ns'] = args.settings.stat().st_mtime_ns
            samples.append(sample)
            # Never send a burst of catch-up events after a slow request.
            due = max(due + 1 / args.hz, end)
    finally:
        if args.mode == 'resize':
            client.request('resize_window', {'target': args.window, 'w': args.size[0],
                                             'h': args.size[1], 'snapshot': False})
    values = sorted(sample['rtt_ms'] for sample in samples)
    return {
        'session': args.session, 'pid': client.config['pid'], 'mode': args.mode,
        'scope': 'Local TCP/message-thread round trip, not paint duration, visible-frame latency, or native live resize. '
                 'One Python process; snapshots disabled for resize; no screenshots or focus changes.',
        'requested_seconds': args.seconds, 'target_hz': args.hz,
        'window': args.window, 'base_size': args.size, 'resize_amplitude': args.amplitude,
        'samples': samples,
        'summary': {'count': len(values), 'p50_ms': values[len(values) // 2],
                    'p99_ms': values[int(.99 * (len(values) - 1))], 'max_ms': max(values),
                    'over_33_ms': sum(value > 33 for value in values)},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', required=True, help='Exact existing Jucewright session name')
    parser.add_argument('--sessions-dir', type=Path,
                        default=Path(os.environ.get('TMPDIR', '/tmp')) / 'jucewright/sessions')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('ping', 'resize'), default='resize')
    parser.add_argument('--seconds', type=positive_float, default=10)
    parser.add_argument('--hz', type=positive_float, default=60)
    parser.add_argument('--window', default='root')
    parser.add_argument('--size', type=nonnegative_int, nargs=2, default=[1100, 770],
                        metavar=('WIDTH', 'HEIGHT'), help='Native window base size, also restored after resize')
    parser.add_argument('--amplitude', type=nonnegative_int, nargs=2, default=[100, 70],
                        metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--settings', type=Path, help='Optional isolated settings file for write-time correlation')
    args = parser.parse_args()
    if any(size <= amplitude for size, amplitude in zip(args.size, args.amplitude)):
        parser.error('Each base size must exceed its resize amplitude')
    client = Client(args.session, args.sessions_dir)
    client.request('ping')  # Validate the session outside the measured interval.
    result = measure(client, args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result['summary']))


if __name__ == '__main__':
    main()
