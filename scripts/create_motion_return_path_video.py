#!/usr/bin/env python3
"""Original bridge activation movie for Return Path; no project data is written."""
import argparse
import math
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
width, height, fps, duration = 512, 256, 30, 38.4
amber, cyan = (255, 155, 45), (55, 205, 255)


def pixel(x, y):
    return (round(width / 2 + x / .6 * width / 2), round(height / 2 - y / .6 * width / 2))


def smooth(t):
    t = min(1, max(0, t))
    return t * t * (3 - 2 * t)


def carrier(t):
    if t < 12.8:
        return -.4 + .3 * smooth(t / 12.8)
    if t < 25.6:
        return -.1 + .45 * smooth((t - 12.8) / 12.8)
    return .35 - .2 * smooth((t - 25.6) / 12.8)


encoder = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
    '-s', f'{width}x{height}', '-r', str(fps), '-i', '-', '-an', '-c:v', 'libx264', '-crf', '14',
    '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(args.output)], stdin=subprocess.PIPE)
try:
    for frame in range(round(duration * fps)):
        t = frame / fps
        image = Image.new('RGB', (width, height), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        # Four probes extend the route. The final probe reaches the receiver;
        # its cyan acknowledgement travels back along the completed lower rail.
        reached = -.4 if t < 2.4 else min(.4, -.4 + .2 * math.floor((t - 2.4) / 3.2 + 1))
        retract = -.4 + .8 * smooth((t - 25.6) / 10.8)
        left = max(-.4, retract) if t >= 25.6 else -.4
        if reached > left:
            for segment in range(4):
                a, b = max(left, -.4 + segment * .2), min(reached, -.2 + segment * .2)
                if b <= a:
                    continue
                acknowledged = t >= 12 + (.4 - (a + b) / 2) / .5
                colour = (55, 180, 230) if acknowledged else (180, 115, 45)
                draw.line([pixel(a, -.12), pixel(b, -.12)], fill=colour, width=2)
                if a <= -.4 + segment * .2 + .0001:
                    draw.line([pixel(a, -.12), pixel(a, -.065)], fill=colour, width=2)
        head = None
        if .8 <= t < 12:
            cycle = min(3, int((t - .8) / 3.2))
            age = (t - .8) - cycle * 3.2
            target = -.2 + cycle * .2
            origin = carrier(.8 + cycle * 3.2)
            travel = age / 1.6 if age <= 1.6 else 2 - age / 1.6
            head = (origin + (target - origin) * travel, .04, amber, 1 if age <= 1.6 else -1)
        elif 12 <= t < 13.6:
            head = (.4 - .8 * (t - 12) / 1.6, -.12, cyan, -1)
        elif 14.4 <= t < 25.6:
            age = (t - 14.4) % 1.6
            origin = carrier(t)
            target = .4 - .05 * smooth(t / 25.6)
            if age < .8:
                head = (origin + (target - origin) * age / .8, .04, amber, 1)
            else:
                head = (target + (origin - target) * (age - .8) / .8, -.12, cyan, -1)
        if head:
            x, y, colour, direction = head
            # A short connected tail keeps the leading packet legible without
            # adding ambient particle spray. Its head follows the route endpoint.
            draw.line([pixel(x - direction * .055, y), pixel(x, y)], fill=colour, width=3)
            draw.line([pixel(x - direction * .02, y + .02), pixel(x, y), pixel(x - direction * .02, y - .02)], fill=colour, width=2)
        encoder.stdin.write(image.tobytes())
finally:
    encoder.stdin.close()
    if encoder.wait() != 0:
        raise SystemExit('Video generation failed')
print(args.output)
