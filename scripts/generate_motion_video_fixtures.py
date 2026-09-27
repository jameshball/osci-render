#!/usr/bin/env python3
"""Generate small deterministic video inputs for Motion video-import tests."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


FRAME_RATE = 24
DURATION_SECONDS = 2
FRAME_COUNT = FRAME_RATE * DURATION_SECONDS
WIDTH = 160
HEIGHT = 120


def set_pixel(pixels: bytearray, x: int, y: int, colour: tuple[int, int, int]) -> None:
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        index = (y * WIDTH + x) * 3
        pixels[index:index + 3] = bytes(colour)


def draw_rect(pixels: bytearray, left: int, top: int, width: int, height: int, colour: tuple[int, int, int]) -> None:
    for x in range(left, left + width):
        set_pixel(pixels, x, top, colour)
        set_pixel(pixels, x, top + height - 1, colour)
    for y in range(top, top + height):
        set_pixel(pixels, left, y, colour)
        set_pixel(pixels, left + width - 1, y, colour)


def draw_mark(pixels: bytearray, frame: int) -> None:
    colour = (255, 0, 0) if frame < FRAME_COUNT // 2 else (0, 0, 255)
    centre_x, centre_y = WIDTH // 2, HEIGHT // 2
    for offset in range(-6, 7):
        set_pixel(pixels, centre_x + offset, centre_y, colour)
        set_pixel(pixels, centre_x, centre_y + offset, colour)


def write_frame(path: Path, frame: int) -> None:
    pixels = bytearray(WIDTH * HEIGHT * 3)
    rectangle_left = 8 + (WIDTH - 56) * frame // (FRAME_COUNT - 1)
    draw_rect(pixels, rectangle_left, 34, 48, 52, (255, 255, 255))
    draw_mark(pixels, frame)
    path.write_bytes(f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii") + pixels)


def run_ffmpeg(ffmpeg: str, frames: Path, output: Path, extra_video_options: list[str] | None = None, frame_count: int = FRAME_COUNT) -> None:
    command = [
        ffmpeg,
        "-y",
        "-loglevel",
        "error",
        "-framerate",
        str(FRAME_RATE),
        "-i",
        str(frames / "frame-%03d.ppm"),
        "-frames:v",
        str(frame_count),
        "-c:v",
        "libx264",
        "-pix_fmt",
        "yuv420p",
    ]
    if extra_video_options:
        command.extend(extra_video_options)
    command.append(str(output))
    subprocess.run(command, check=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("/private/tmp/motion-video-fixtures"))
    parser.add_argument("--ffmpeg", help="Path to FFmpeg; defaults to ffmpeg found on PATH.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    ffmpeg = args.ffmpeg or shutil.which("ffmpeg")
    if ffmpeg is None:
        raise SystemExit("FFmpeg was not found on PATH. Pass --ffmpeg /path/to/ffmpeg.")

    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="motion-video-frames-") as temporary_directory:
        frames = Path(temporary_directory)
        for frame in range(FRAME_COUNT):
            write_frame(frames / f"frame-{frame:03d}.ppm", frame)
        run_ffmpeg(ffmpeg, frames, output_dir / "motion.mp4")
        run_ffmpeg(ffmpeg, frames, output_dir / "motion.mov")
        run_ffmpeg(ffmpeg, frames, output_dir / "single-frame.mp4", frame_count=1)
        run_ffmpeg(ffmpeg, frames, output_dir / "motion-anamorphic-sar2-1.mp4", ["-vf", "setsar=2/1"])

    (output_dir / "corrupt.mp4").write_bytes(b"This is intentionally not an MP4 fixture.\n")
    print(f"FFmpeg: {Path(ffmpeg).resolve()}")
    for name in ("motion.mp4", "motion.mov", "single-frame.mp4", "motion-anamorphic-sar2-1.mp4", "corrupt.mp4"):
        print(output_dir / name)


if __name__ == "__main__":
    main()
