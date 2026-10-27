#!/usr/bin/env python3
"""Verify video exported from create_motion_export_fixture.py using FFmpeg."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess


def run(*arguments):
    return subprocess.run(arguments, check=True, capture_output=True).stdout


def verify(path):
    metadata = json.loads(run("ffprobe", "-v", "error", "-count_frames", "-show_streams", "-show_format", "-of", "json", str(path)))
    video = next(stream for stream in metadata["streams"] if stream["codec_type"] == "video")
    audio = next(stream for stream in metadata["streams"] if stream["codec_type"] == "audio")
    assert int(video["nb_read_frames"]) == 72, video
    assert video["r_frame_rate"] == "24/1", video
    assert abs(float(video["duration"]) - 3) < 1 / 24, video
    assert audio["channels"] == 2, audio
    assert int(audio["sample_rate"]) == 48000, audio
    colors = []
    for index, time in enumerate((0.5, 1.5, 2.5)):
        pixels = run("ffmpeg", "-v", "error", "-ss", str(time), "-i", str(path), "-frames:v", "1", "-vf", "scale=128:128", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1")
        energy = [sum(pixels[channel::3]) for channel in range(3)]
        assert energy[index] > 1000, (time, energy)
        assert all(energy[index] > energy[other] * 1.2 for other in range(3) if other != index), (time, energy)
        colors.append(energy)
    pcm = run("ffmpeg", "-v", "error", "-i", str(path), "-vn", "-acodec", "pcm_f32le", "-f", "f32le", "pipe:1")
    frames = list(struct.iter_unpack("<ff", pcm))
    assert abs(len(frames) - 144000) <= 1024, len(frames)
    # Test a central one-second window, away from AAC priming/tail padding.
    window = frames[48000:96000]
    def amplitude(channel, frequency):
        real = sum(frame[channel] * math.cos(2 * math.pi * frequency * i / 48000) for i, frame in enumerate(window))
        imaginary = sum(frame[channel] * math.sin(2 * math.pi * frequency * i / 48000) for i, frame in enumerate(window))
        return 2 * math.hypot(real, imaginary) / len(window)
    left = amplitude(0, 220)
    right = amplitude(1, 440)
    assert 0.18 < left < 0.30, left
    assert 0.13 < right < 0.23, right
    assert amplitude(0, 440) < 0.02 and amplitude(1, 220) < 0.02, "Soundtrack channels mixed or replaced"
    report = {"frames": 72, "fps": 24, "size": [video["width"], video["height"]], "rgb_energy": colors,
              "audio_channels": 2, "audio_samples": len(frames), "left_220hz": left, "right_440hz": right}
    print(json.dumps(report, indent=2))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path)
    args = parser.parse_args()
    verify(args.video)
