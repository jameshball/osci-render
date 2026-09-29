#!/usr/bin/env python3
"""Create the source media for "dah" v4, the osci-motion benchmark film.

No text and no mouth: the film is carried by geometry. Sources cover every
kind Motion imports: line OBJ, SVG, Lua (one with animated sliders), an
L-system fractal, a Lottie animation, an animated GIF, an MP4 traced as a
raster, MIDI and the user's track (a 48 kHz working copy; the original is
never touched).

The star field is a Lua source: each star travels toward the camera on its
own, fades in from the far end and wraps behind every camera, and the whole
field repeats exactly over its 6.4 s bake, so it loops with no visible seam.
Jumps between stars are drawn black.

Usage: python3 scripts/create_motion_dah3_assets.py <destination> [--music <flac>]
"""
import argparse
import json
import math
import random
import shutil
import subprocess
import tempfile
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
    return list(vertices), sum(len(line) - 1 for line in lines)


def circle(radius, segments, z=0.0, phase=0.0):
    return [(radius * math.cos(phase + 2 * math.pi * i / segments), radius * math.sin(phase + 2 * math.pi * i / segments), z)
            for i in range(segments + 1)]


def tunnel():
    # Rings every 2 units along -Z joined by four rails; a camera moving 2
    # units per cycle loops seamlessly because the corridor repeats.
    rings = [circle(1.0, 16, -2.0 * k, math.pi / 16) for k in range(8)]
    rails = [[(math.cos(a), math.sin(a), 0.0), (math.cos(a), math.sin(a), -14.0)] for a in (math.pi / 4, 3 * math.pi / 4, 5 * math.pi / 4, 7 * math.pi / 4)]
    return rings + rails


def icosahedron():
    """A plain icosahedron: 30 edges keep the beam free for the star field."""
    t = (1 + 5 ** 0.5) / 2
    base = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
            (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
             (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]

    def unit(p):
        length = math.sqrt(sum(c * c for c in p))
        return tuple(round(c / length, 6) for c in p)
    edges = set()
    for face in faces:
        for i in range(3):
            edges.add(tuple(sorted((unit(base[face[i]]), unit(base[face[(i + 1) % 3]])))))
    return [list(edge) for edge in sorted(edges)]


def torus_knot(p=2, q=3, segments=240):
    points = []
    for i in range(segments + 1):
        t = 2 * math.pi * i / segments
        r = 0.6 + 0.28 * math.cos(q * t)
        points.append((r * math.cos(p * t), r * math.sin(p * t), 0.28 * math.sin(q * t)))
    return [points]


def svg(path, body, comment):
    path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-100 -100 200 200"><!-- {comment} -->{body}</svg>\n')


def polygon_points(sides, radius, rotation=-90, inner=None):
    points = []
    count = sides * (2 if inner else 1)
    for i in range(count):
        r = inner if inner and i % 2 else radius
        a = math.radians(rotation + 360 * i / count)
        points.append(f"{r * math.cos(a):.2f},{r * math.sin(a):.2f}")
    return " ".join(points)


LUA = {
    # A flatline that becomes a sine: the opening trace.
    "Trace.lua": """
local grow = math.min(1, play_time / 6.4)
local u = phase / (2 * math.pi)
return {-1 + 2 * u, grow * 0.18 * math.sin(2 * math.pi * 3 * u + 2 * math.pi * play_time * 150 / 60 / 2), 0, 0.4, 1, 0.55}
""",
    # A damped two-pendulum harmonograph. Slider A sweeps the frequency ratio
    # from 3:2 to 2:1 (the figure unwinds and closes again), slider B sets how
    # quickly it decays toward the centre.
    "Harmonograph.lua": """
local u = phase / (2 * math.pi)
local t = u * 8 * math.pi
local ratio = 1.5 + 0.5 * slider_a
local decay = math.exp(-(0.3 + 1.5 * slider_b) * u)
local spin = 2 * math.pi * play_time / 6.4
local x = decay * (math.sin(2 * t + spin) + math.sin(2 * ratio * t))
local y = decay * (math.sin(3 * t) + math.cos(2 * ratio * t + spin))
return {0.4 * x, 0.4 * y, 0, 1, 0.6 + 0.3 * slider_a, 0.3 + 0.3 * slider_b}
""",
    # 64 stars stream toward the camera. Each travels 21 units over the
    # 6.4 s bake and wraps behind every camera (z > 5), so the field repeats
    # exactly and loops seamlessly; stars fade in over the first fifth of
    # their trip. Jumps between stars are drawn black.
    "Starfield.lua": """
local N, T, L, far = 64, 6.4, 21, -16
local u = phase / (2 * math.pi)
local i = math.min(N - 1, math.floor(u * N))
local v = u * N - i
local function hash(n)
  local x = math.sin(n * 12.9898 + 78.233) * 43758.5453
  return x - math.floor(x)
end
local a = 2 * math.pi * hash(3 * i + 1)
local r = 0.45 + 2.6 * math.sqrt(hash(3 * i + 2))
local cx, cy = r * math.cos(a), 0.65 * r * math.sin(a)
local f = (hash(3 * i + 3) + play_time / T) % 1
local z = far + L * f
local fade = math.min(1, f / 0.2)
local s = (0.07 + 0.07 * hash(7 * i + 5)) * (0.4 + 0.6 * fade)
-- Each arm is drawn once; the beam moves between arms and stars in black.
local px, py, lit = 0, 0, fade
if v < 0.05 then
  px, lit = -s, 0
elseif v < 0.45 then
  px = -s + 2 * s * (v - 0.05) / 0.4
elseif v < 0.5 then
  px, lit = s, 0
elseif v < 0.55 then
  py, lit = s, 0
elseif v < 0.95 then
  py = s - 2 * s * (v - 0.55) / 0.4
else
  py, lit = -s, 0
end
return {cx + px, cy + py, z, 0.85 * lit, 0.95 * lit, lit}
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
}


def lsystems(out):
    # A branching fern; depth is chosen when preparing it in the app.
    fern = {"axiom": "X", "angle": 22.5,
            "rules": [{"variable": "X", "replacement": "F+[[X]-X]-F[-FX]+X"}, {"variable": "F", "replacement": "FF"}]}
    (out / "Fern.lsystem").write_text(json.dumps(fern, indent=2) + "\n")
    # A closed quadratic Koch island for the finale frame.
    island = {"axiom": "F+F+F+F", "angle": 90, "rules": [{"variable": "F", "replacement": "F+F-F-FF+F+F-F"}]}
    (out / "Island.lsystem").write_text(json.dumps(island, indent=2) + "\n")


def keyframes(values, frames, ease=True):
    """[(frame, value)] as Lottie keyframes; values may be scalars or lists."""
    keys = []
    for frame, value in values:
        key = {"t": frame, "s": value if isinstance(value, list) else [value]}
        if ease:
            key["i"] = {"x": [0.4], "y": [1]}
            key["o"] = {"x": [0.6], "y": [0]}
        keys.append(key)
    return {"a": 1, "k": keys}


def static(value):
    return {"a": 0, "k": value}


def lottie_bloom(path):
    """One bar at 30 fps: a six-point star opens and turns while a ring draws itself on."""
    frames = 48
    transform = {"ty": "tr", "p": static([0, 0]), "a": static([0, 0]), "s": static([100, 100]), "r": static(0), "o": static(100)}
    star = {"ty": "gr", "nm": "star", "it": [
        {"ty": "sr", "sy": 1, "d": 1, "pt": static(6), "p": static([0, 0]), "r": keyframes([(0, 0), (frames, 60)], frames),
         "ir": keyframes([(0, 30), (24, 70), (frames, 30)], frames), "is": static(0),
         "or": keyframes([(0, 90), (24, 150), (frames, 90)], frames), "os": static(0)},
        {"ty": "st", "c": static([1, 1, 1, 1]), "o": static(100), "w": static(6), "lc": 2, "lj": 2},
        transform]}
    ring = {"ty": "gr", "nm": "ring", "it": [
        {"ty": "el", "d": 1, "p": static([0, 0]), "s": static([380, 380])},
        {"ty": "tm", "s": static(0), "e": keyframes([(0, 0), (frames, 100)], frames), "o": keyframes([(0, 0), (frames, 180)], frames, ease=False), "m": 1},
        {"ty": "st", "c": static([1, 1, 1, 1]), "o": static(100), "w": static(6), "lc": 2, "lj": 2},
        transform]}
    layer = {"ddd": 0, "ind": 1, "ty": 4, "nm": "bloom", "sr": 1, "ip": 0, "op": frames, "st": 0, "bm": 0,
             "ks": {"o": static(100), "r": static(0), "p": static([256, 256, 0]), "a": static([0, 0, 0]), "s": static([100, 100, 100])},
             "shapes": [star, ring]}
    doc = {"v": "5.7.0", "fr": 30, "ip": 0, "op": frames, "w": 512, "h": 512, "nm": "Bloom", "ddd": 0, "assets": [], "layers": [layer]}
    path.write_text(json.dumps(doc) + "\n")


def ripples_video(path, frames=48, size=256):
    """One bar at 30 fps: rings expand from the centre inside a turning square."""
    from PIL import Image, ImageDraw
    with tempfile.TemporaryDirectory() as folder:
        for f in range(frames):
            image = Image.new("L", (size, size), 0)
            draw = ImageDraw.Draw(image)
            c = size / 2
            for k in range(3):
                r = ((f / frames + k / 3) % 1) * size * 0.45 + 4
                draw.ellipse([c - r, c - r, c + r, c + r], outline=255, width=3)
            a = 2 * math.pi * f / frames / 4
            corners = [(c + size * 0.46 * math.cos(a + math.pi / 4 + i * math.pi / 2), c + size * 0.46 * math.sin(a + math.pi / 4 + i * math.pi / 2)) for i in range(4)]
            draw.line(corners + [corners[0]], fill=255, width=3)
            image.save(Path(folder) / f"f{f:03d}.png")
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-framerate", "30", "-i", str(Path(folder) / "f%03d.png"),
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", str(path)], check=True)


def melody_midi(path):
    """A "do da dahh" answer phrase for the breakdown, 8 bars at 150 BPM."""
    division = 480
    phrase = [(0, 1, 76, 90), (1, 1, 79, 90), (2, 2, 84, 110),
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
    manifest = {"film": "dah v4", "bpm": BPM, "bar": BAR, "assets": []}

    def record(name, kind, detail):
        manifest["assets"].append({"file": name, "kind": kind, "detail": detail})

    for name, lines, note in [("Tunnel.obj", tunnel(), "8 rings on -Z, repeating every 2 units"),
                              ("Globe.obj", icosahedron(), "icosahedron"),
                              ("Knot.obj", torus_knot(), "(2,3) torus knot")]:
        vertices, edges = write_obj(out / name, lines, note)
        record(name, "OBJ", f"{note}; {len(vertices)} vertices, {edges} edges")
    svg(out / "Dot.svg", '<circle cx="0" cy="0" r="3" fill="none" stroke="white"/>', "a single point of light")
    record("Dot.svg", "SVG", "tiny circle")
    svg(out / "Ring.svg", '<circle cx="0" cy="0" r="60" fill="none" stroke="white"/>', "note ring for MIDI")
    record("Ring.svg", "SVG", "circle traced at note pitch")
    svg(out / "Triangle.svg", f'<polygon points="{polygon_points(3, 80)}" fill="none" stroke="white"/>', "do")
    svg(out / "Square.svg", f'<polygon points="{polygon_points(4, 80, rotation=-45)}" fill="none" stroke="white"/>', "da")
    svg(out / "Burst.svg", f'<polygon points="{polygon_points(8, 90, inner=38)}" fill="none" stroke="white"/>', "dahh")
    for name, detail in [("Triangle.svg", "beat-one hit"), ("Square.svg", "beat-two hit"), ("Burst.svg", "beat-four burst")]:
        record(name, "SVG", detail)
    for name, source in LUA.items():
        (out / name).write_text(source.strip() + "\n")
        record(name, "Lua", "sliders A (ratio) and B (decay) animate per clip" if "slider_" in source else "loops on bar multiples at 150 BPM")
    lsystems(out)
    record("Fern.lsystem", "L-system", "branching fern")
    record("Island.lsystem", "L-system", "quadratic Koch island")
    lottie_bloom(out / "Bloom.json")
    record("Bloom.json", "Lottie", "six-point star and self-drawing ring, one bar")
    ripples_video(out / "Ripples.mp4")
    record("Ripples.mp4", "Video", "expanding rings in a turning square, one bar")
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
