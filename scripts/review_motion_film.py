#!/usr/bin/env python3
"""Extract labelled review frames from an exported film and check its streams.

Usage: python3 scripts/review_motion_film.py <video> <output-dir> [--every 6.4] [--times 1.6,48.1,...]
Writes frames/<seconds>.png, a contact sheet per 12 frames and report.json
(duration, frame rate, frame count, audio channels, mean frame brightness).
"""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("video", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--every", type=float, default=6.4)
parser.add_argument("--times", type=str, default="")
parser.add_argument("--columns", type=int, default=4)
args = parser.parse_args()
ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
if not ffmpeg or not ffprobe:
    raise SystemExit("ffmpeg and ffprobe must be on PATH")
frames = args.output / "frames"
frames.mkdir(parents=True, exist_ok=True)
probe = json.loads(subprocess.check_output([ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", str(args.video)], text=True))
duration = float(probe["format"]["duration"])
video = next(s for s in probe["streams"] if s["codec_type"] == "video")
audio = next((s for s in probe["streams"] if s["codec_type"] == "audio"), None)
times = [float(t) for t in args.times.split(",") if t] or [round(t * args.every + args.every / 2, 3) for t in range(int(duration // args.every))]

from PIL import Image, ImageDraw, ImageStat
stats = []
for moment in times:
    path = frames / f"{moment:07.2f}.png"
    subprocess.run([ffmpeg, "-v", "error", "-y", "-ss", str(moment), "-i", str(args.video), "-frames:v", "1", str(path)], check=True)
    image = Image.open(path).convert("RGB")
    stats.append({"time": moment, "meanBrightness": round(sum(ImageStat.Stat(image).mean) / 3, 2)})
tiles = []
for moment in times:
    image = Image.open(frames / f"{moment:07.2f}.png").convert("RGB")
    image.thumbnail((480, 270))
    ImageDraw.Draw(image).text((8, 6), f"{moment:.2f}s  bar {moment / 1.6:.1f}", fill=(255, 255, 255))
    tiles.append(image)
per_sheet = args.columns * 3
for sheet_index in range(0, len(tiles), per_sheet):
    group = tiles[sheet_index:sheet_index + per_sheet]
    width, height = group[0].size
    rows = (len(group) + args.columns - 1) // args.columns
    sheet = Image.new("RGB", (width * args.columns, height * rows))
    for index, tile in enumerate(group):
        sheet.paste(tile, ((index % args.columns) * width, (index // args.columns) * height))
    sheet.save(args.output / f"sheet-{sheet_index // per_sheet + 1:02d}.png")
report = {"video": str(args.video), "duration": duration, "width": video["width"], "height": video["height"],
          "frameRate": video["avg_frame_rate"], "audioChannels": audio["channels"] if audio else 0, "frames": stats}
(args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps({k: report[k] for k in ("duration", "width", "height", "frameRate", "audioChannels")}))
