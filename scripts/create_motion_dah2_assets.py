#!/usr/bin/env python3
"""Create the original source media for "dah", the osci-motion benchmark film.

Everything is generated here: line-only OBJ geometry, SVG shapes, Lua motion
sources (150 BPM, looping on bar multiples), plain-text type hits, an animated
GIF and a 48 kHz FLAC working copy of the user's track. No arrangement is made
here; the film is authored through the application.

Usage: python3 scripts/create_motion_dah2_assets.py <destination> [--music <flac>]
"""
import argparse
import json
import math
import shutil
import subprocess
from pathlib import Path

BPM = 150
BEAT = 60 / BPM
BAR = 4 * BEAT


def write_obj(path, polylines, comment):
    """Polylines of 3D points become OBJ vertices joined by line elements."""
    vertices, lines = {}, []
    for polyline in polylines:
        indices = []
        for point in polyline:
            key = tuple(round(v, 6) for v in point)
            assert all(math.isfinite(v) for v in key)
            if key not in vertices:
                vertices[key] = len(vertices) + 1
            indices.append(vertices[key])
        lines.append(indices)
    with open(path, "w") as out:
        out.write(f"# {comment}\n")
        for vertex in vertices:
            out.write("v %.6f %.6f %.6f\n" % vertex)
        for indices in lines:
            out.write("l " + " ".join(str(i) for i in indices) + "\n")
    return len(vertices), sum(len(line) - 1 for line in lines)


def circle(radius, segments, z=0.0, phase=0.0):
    return [(radius * math.cos(phase + 2 * math.pi * i / segments), radius * math.sin(phase + 2 * math.pi * i / segments), z)
            for i in range(segments + 1)]


def tunnel():
    # Rings every 2 units along -Z joined by four rails. A camera moving 2
    # units per two bars loops seamlessly because the corridor repeats.
    rings = [circle(1.0, 16, -2.0 * k, math.pi / 16) for k in range(8)]
    rails = [[(math.cos(a), math.sin(a), 0.0), (math.cos(a), math.sin(a), -14.0)] for a in (math.pi / 4, 3 * math.pi / 4, 5 * math.pi / 4, 7 * math.pi / 4)]
    return rings + rails


def sphere():
    lines = []
    for lat in range(1, 8):
        theta = math.pi * lat / 8
        lines.append([(math.sin(theta) * math.cos(2 * math.pi * i / 24), math.cos(theta), math.sin(theta) * math.sin(2 * math.pi * i / 24)) for i in range(25)])
    for lon in range(8):
        phi = math.pi * lon / 8
        lines.append([(math.sin(math.pi * i / 24) * math.cos(phi), math.cos(math.pi * i / 24), math.sin(math.pi * i / 24) * math.sin(phi)) for i in range(25)] +
                     [(math.sin(math.pi * i / 24) * math.cos(phi + math.pi), math.cos(math.pi * i / 24), math.sin(math.pi * i / 24) * math.sin(phi + math.pi)) for i in range(23, -1, -1)])
    return lines


def torus_knot(p=2, q=3, segments=240):
    points = []
    for i in range(segments + 1):
        t = 2 * math.pi * i / segments
        r = 0.6 + 0.28 * math.cos(q * t)
        points.append((r * math.cos(p * t), r * math.sin(p * t), 0.28 * math.sin(q * t)))
    return [points]


# A blocky single-stroke-friendly letter set on a 1 x 1.4 grid.
LETTERS = {
    "D": [[(0, 0), (0, 1.4), (0.6, 1.4), (1, 1.0), (1, 0.4), (0.6, 0), (0, 0)]],
    "A": [[(0, 0), (0.5, 1.4), (1, 0)], [(0.22, 0.6), (0.78, 0.6)]],
    "H": [[(0, 0), (0, 1.4)], [(1, 0), (1, 1.4)], [(0, 0.7), (1, 0.7)]],
    "!": [[(0.5, 1.4), (0.5, 0.45)], [(0.42, 0.05), (0.58, 0.05), (0.58, 0.18), (0.42, 0.18), (0.42, 0.05)]],
}


def letters(word="DAHH!", depth=0.35):
    lines, x = [], 0.0
    for char in word:
        width = 0.5 if char == "!" else 1.0
        for stroke in LETTERS[char]:
            front = [(x + px, py, depth / 2) for px, py in stroke]
            back = [(x + px, py, -depth / 2) for px, py in stroke]
            lines += [front, back]
            lines += [[f, b] for f, b in zip(front, back)]
        x += width + 0.35
    offset_x, offset_y = x / 2 - 0.175, 0.7
    return [[(px - offset_x, py - offset_y, pz) for px, py, pz in line] for line in lines]


def stars(count=56, seed=7):
    import random
    random.seed(seed)
    lines = []
    for _ in range(count):
        x, y, z = random.uniform(-3, 3), random.uniform(-2, 2), random.uniform(-6, 1)
        s = random.uniform(0.02, 0.05)
        lines.append([(x - s, y, z), (x + s, y, z)])
        lines.append([(x, y - s, z), (x, y + s, z)])
    return lines


def svg(path, body, comment):
    path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-100 -100 200 200"><!-- {comment} -->{body}</svg>\n')


LUA = {
    # Lips open on every beat ("dah"), with a small tongue arc when open.
    "Mouth.lua": """
local beats = play_time * 150 / 60
local open = math.abs(math.sin(math.pi * beats)) ^ 0.7
local p = phase / (2 * math.pi)
local x, y
if p < 0.42 then
  local u = p / 0.42
  x = -0.6 + 1.2 * u
  y = 0.05 + (0.06 + 0.22 * open) * math.sin(math.pi * u) - 0.04 * math.sin(2 * math.pi * u) ^ 2
elseif p < 0.84 then
  local u = (p - 0.42) / 0.42
  x = 0.6 - 1.2 * u
  y = -0.05 - (0.08 + 0.34 * open) * math.sin(math.pi * u)
else
  local u = (p - 0.84) / 0.16
  x = -0.3 + 0.6 * u
  y = -0.05 - 0.22 * open * math.sin(math.pi * u) ^ 0.5
end
return {x, y, 0, 1, 0.62, 0.22}
""",
    # A 3:2 Lissajous whose phase turns once per four bars.
    "Lissajous.lua": """
local turn = 2 * math.pi * play_time / 6.4
return {0.8 * math.sin(3 * phase + turn), 0.8 * math.sin(2 * phase), 0.3 * math.cos(phase + turn), 0.3, 0.85, 1}
""",
    # A hypotrochoid breathing over eight bars.
    "Spirograph.lua": """
local k = 0.5 + 0.18 * math.sin(2 * math.pi * play_time / 12.8)
local R, r, d = 1, 0.35, 0.55 * k + 0.2
local t = phase * 7
local x = (R - r) * math.cos(t) + d * math.cos((R - r) / r * t)
local y = (R - r) * math.sin(t) - d * math.sin((R - r) / r * t)
return {0.55 * x, 0.55 * y, 0, 0.9, 0.35, 1}
""",
    # A flatline that becomes a sine: the opening trace.
    "Trace.lua": """
local grow = math.min(1, play_time / 6.4)
local u = phase / (2 * math.pi)
return {-1 + 2 * u, grow * 0.18 * math.sin(2 * math.pi * 3 * u + 2 * math.pi * play_time * 150 / 60 / 2), 0, 0.4, 1, 0.55}
""",
}

TEXT = {
    "dahhhhh.txt": "dahhhhh",
    "do.txt": "do",
    "da.txt": "da",
    "dahh!.txt": "dahh!",
    "dah..txt": "dah.",
}


def melody_midi(path):
    """A "do da dahh" answer phrase for the breakdown, 8 bars at 150 BPM."""
    division = 480
    phrase = [(0, 1, 76, 90), (1, 1, 79, 90), (2, 2, 84, 110),     # do da dahh
              (4, 1, 74, 80), (5, 1, 77, 80), (6, 2, 81, 100),
              (8, 1, 76, 90), (9, 1, 79, 90), (10, 1, 84, 105), (11, 1, 86, 110),
              (12, 4, 88, 120)]
    events = []
    for repeat in range(2):
        for beat, length, pitch, velocity in phrase:
            start = (repeat * 16 + beat) * division
            events.append((start, 0x90, pitch, velocity))
            events.append((start + length * division - 30, 0x80, pitch, 0))
    events.sort(key=lambda event: (event[0], event[1]))
    data, last = bytearray(), 0

    def vlq(value):
        out = [value & 0x7f]
        value >>= 7
        while value:
            out.append((value & 0x7f) | 0x80)
            value >>= 7
        return bytes(reversed(out))
    tempo = int(60_000_000 / BPM)
    data += vlq(0) + bytes([0xff, 0x51, 3]) + tempo.to_bytes(3, "big")
    for time, status, pitch, velocity in events:
        data += vlq(time - last) + bytes([status, pitch, velocity])
        last = time
    data += vlq(0) + bytes([0xff, 0x2f, 0])
    path.write_bytes(b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big") + division.to_bytes(2, "big")
                     + b"MTrk" + len(data).to_bytes(4, "big") + bytes(data))


def equaliser_gif(path, frames=16, size=96):
    from PIL import Image, ImageDraw
    images = []
    for f in range(frames):
        image = Image.new("L", (size, size), 0)
        draw = ImageDraw.Draw(image)
        for bar in range(6):
            level = 0.2 + 0.8 * abs(math.sin(math.pi * (f / frames * 2 + bar * 0.37)))
            x = 8 + bar * 14
            draw.rectangle([x, size - 8 - level * (size - 20), x + 8, size - 8], fill=255)
        images.append(image)
    images[0].save(path, save_all=True, append_images=images[1:], duration=int(BEAT * 1000 / 4), loop=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--music", type=Path, default=Path("/Users/james/Downloads/dahhhhh do da dahh!.flac"))
    args = parser.parse_args()
    out = args.destination
    out.mkdir(parents=True, exist_ok=True)
    manifest = {"film": "dah", "bpm": BPM, "bar": BAR, "assets": []}

    def record(name, kind, detail):
        manifest["assets"].append({"file": name, "kind": kind, "detail": detail})

    for name, lines, note in [("Tunnel.obj", tunnel(), "8 rings on -Z, repeating every 2 units"),
                              ("Sphere.obj", sphere(), "7 latitudes, 8 meridians"),
                              ("Knot.obj", torus_knot(), "(2,3) torus knot"),
                              ("DAHH.obj", letters(), "extruded block letters"),
                              ("Stars.obj", stars(), "56 crosses in a 3D volume")]:
        vertices, edges = write_obj(out / name, lines, note)
        record(name, "OBJ", f"{note}; {vertices} vertices, {edges} edges")
    svg(out / "Dot.svg", '<circle cx="0" cy="0" r="3" fill="none" stroke="white"/>', "a single point of light")
    record("Dot.svg", "SVG", "tiny circle")
    svg(out / "Ring.svg", '<circle cx="0" cy="0" r="60" fill="none" stroke="white"/>', "note ring for MIDI")
    record("Ring.svg", "SVG", "circle traced at note pitch")
    for name, source in LUA.items():
        (out / name).write_text(source.strip() + "\n")
        record(name, "Lua", "loops on bar multiples at 150 BPM")
    for name, text in TEXT.items():
        (out / name).write_text(text + "\n")
        record(name, "Text", text)
    melody_midi(out / "Melody.mid")
    record("Melody.mid", "MIDI", "8-bar do-da-dahh answer phrase, repeated once")
    equaliser_gif(out / "Equaliser.gif")
    record("Equaliser.gif", "GIF", "6 bars, 16 frames, one frame per sixteenth")
    if args.music.exists():
        music = out / "dah 48k.flac"
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(args.music), "-ar", "48000", "-sample_fmt", "s16", str(music)], check=True)
        record(music.name, "Audio", "48 kHz working copy of the user's track; the original is untouched")
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Created {len(manifest['assets'])} assets in {out}")


if __name__ == "__main__":
    main()
