#!/usr/bin/env python3
"""Check a full Return Path export; artistic acceptance requires separate review."""
import argparse
import json
import subprocess
from pathlib import Path
import numpy as np


def run(*args):
    return subprocess.run(args, capture_output=True, check=True).stdout


def verify(movie, soundtrack):
    metadata = json.loads(run('ffprobe', '-v', 'error', '-count_frames', '-show_streams', '-of', 'json', str(movie)))
    video = next(s for s in metadata['streams'] if s['codec_type'] == 'video')
    audio = next(s for s in metadata['streams'] if s['codec_type'] == 'audio')
    assert (video['width'], video['height'], video['r_frame_rate']) == (1280, 720, '30/1'), video
    assert int(video['nb_read_frames']) == 5568, video
    assert abs(float(video['duration']) - 185.6) < 1 / 30, video
    assert audio['channels'] == 2 and int(audio['sample_rate']) == 48000, audio
    run('ffmpeg', '-v', 'error', '-xerror', '-i', str(movie), '-f', 'null', '-')

    def pcm(path):
        return np.frombuffer(run('ffmpeg', '-v', 'error', '-i', str(path), '-vn', '-t', '185.6',
            '-ar', '48000', '-ac', '2', '-f', 'f32le', '-'), dtype='<f4').reshape(-1, 2)

    actual, original = pcm(movie), pcm(soundtrack)
    assert len(actual) >= round(185.6 * 48000), len(actual)
    length = min(len(actual), len(original))
    # Exclude encoder boundaries; verify each channel separately, preserving stereo.
    correlations = [float(np.corrcoef(actual[48000:length-48000, c], original[48000:length-48000, c])[0, 1]) for c in range(2)]
    assert min(correlations) > .999, correlations
    return {'movie': str(movie), 'frames': 5568, 'duration': 185.6, 'size': [1280, 720],
            'fps': 30, 'stereo_correlations': correlations, 'full_decode': 'passed',
            'artistic_acceptance': 'not established by this check'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('movie', type=Path)
    parser.add_argument('soundtrack', type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.movie, args.soundtrack), indent=2))
