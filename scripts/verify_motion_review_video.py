#!/usr/bin/env python3
"""Check a finished review movie against its soundtrack, retaining evidence."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('video', type=Path)
parser.add_argument('soundtrack', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--duration', type=float, required=True)
args = parser.parse_args()
ffmpeg, ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    raise SystemExit('ffmpeg and ffprobe must be on PATH')
args.output.mkdir(parents=True, exist_ok=True)
probe = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-count_frames', '-show_streams', '-show_format', '-of', 'json', str(args.video)], text=True))
video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
audio = next(s for s in probe['streams'] if s['codec_type'] == 'audio')
assert abs(float(probe['format']['duration']) - args.duration) < .05, probe['format']['duration']
assert (video['width'], video['height']) == (1280, 720)
assert video['avg_frame_rate'] == '30/1'
assert int(video['nb_read_frames']) == round(args.duration * 30)
assert audio['channels'] == 2

def decode(path):
    raw = subprocess.check_output([ffmpeg, '-v', 'error', '-i', str(path), '-map', '0:a:0', '-ac', '2', '-ar', '48000', '-f', 'f32le', '-'])
    samples = np.frombuffer(raw, dtype='<f4').reshape(-1, 2)
    assert np.isfinite(samples).all()
    return samples

actual, reference = decode(args.video), decode(args.soundtrack)
expected = round(args.duration * 48000)
# AAC decoders can expose final codec padding; assess the full authored region.
assert len(actual) >= expected and len(actual) - expected <= 2048, len(actual)
assert len(reference) == expected, len(reference)
actual = actual[:expected]
correlation = [float(np.corrcoef(actual[:, channel], reference[:, channel])[0, 1]) for channel in range(2)]
assert min(correlation) > .995, correlation
report = {'video': str(args.video.resolve()), 'duration': args.duration, 'frames': int(video['nb_read_frames']),
          'audio_frames': expected, 'soundtrack_correlation': correlation, 'audio_peak': float(np.abs(actual).max()),
          'scope': 'Decode/metadata and waveform agreement; does not establish visual pacing or artistic quality.'}
(args.output / 'video-validation.json').write_text(json.dumps(report, indent=2) + '\n')
(args.output / 'ffprobe.json').write_text(json.dumps(probe, indent=2) + '\n')
for time in (3.2, 16, 35.2, 54.4, 67.2, 80, 99.2, 118.4, 137.6, 156.8, 172.8, 180, 185):
    if time >= args.duration:
        continue
    subprocess.run([ffmpeg, '-v', 'error', '-ss', str(time), '-i', str(args.video), '-frames:v', '1', '-update', '1', '-y', str(args.output / f'frame-{time:06.1f}.png')], check=True)
print(json.dumps(report, indent=2))
