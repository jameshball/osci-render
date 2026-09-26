#!/usr/bin/env python3
"""Generate original Phase / Space inputs; never construct an arranged project."""
import argparse
import math
import struct
from pathlib import Path
from PIL import Image, ImageDraw


def create_assets(destination):
    destination.mkdir(parents=True, exist_ok=True)

    def obj(name, paths):
        vertices, edges = {}, []
        for path in paths:
            indices = []
            for point in path:
                point = tuple(round(value, 9) for value in point)
                if point not in vertices:
                    vertices[point] = len(vertices) + 1
                indices.append(vertices[point])
            edges.append("l " + " ".join(str(index) for index in indices))
        lines = [f"v {x:.9f} {y:.9f} {z:.9f}" for x, y, z in vertices]
        lines.extend(edges)
        (destination / name).write_text("\n".join(lines) + "\n")

    def ring(radius, z=0, count=96):
        return [(radius * math.cos(i * math.tau / count), radius * math.sin(i * math.tau / count), z) for i in range(count + 1)]

    obj("Hero diamond.obj", [[(0, .65, 0), (.42, 0, 0), (0, -.65, 0), (-.42, 0, 0), (0, .65, 0)]])
    obj("Inner ring.obj", [ring(.72)])
    obj("Outer ring.obj", [ring(.95)])
    obj("Orbit crown.obj", [ring(.9, .24), ring(.9, -.24)])
    for side in ("Left", "Right"):
        obj(side + " satellite.obj", [[(0, .14, 0), (.14, 0, 0), (0, -.14, 0), (-.14, 0, 0), (0, .14, 0)]])
    obj("Corridor.obj", [[(-.75, -.75, z), (.75, -.75, z), (.75, .75, z), (-.75, .75, z), (-.75, -.75, z)] for z in (0, .5, 1, 1.5, 2, 2.5, 3)])
    (destination / "Horizon.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100"><path d="M10 50 H190 M90 42 L100 50 L110 42" fill="none" stroke="white"/></svg>')
    (destination / "MIDI pulse.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M50 10 L90 50 L50 90 L10 50 Z" fill="none" stroke="white"/></svg>')
    (destination / "Title.txt").write_text("PHASE / SPACE")
    (destination / "Accent.txt").write_text("01 : ORBIT")
    (destination / "Ribbons.lua").write_text('return {0.7*math.cos(phase), 0.55*math.sin(2*phase), 0.2*math.sin(3*phase), 0.35, 0.65, 1}\n')
    frames = []
    for index in range(24):
        frame = Image.new("P", (96, 96), 0)
        frame.putpalette([0, 0, 0, 80, 210, 255] + [0, 0, 0] * 254)
        draw = ImageDraw.Draw(frame)
        radius = 16 + round(8 * (1 - math.cos(index * math.tau / 24)))
        draw.ellipse((48-radius, 48-radius, 48+radius, 48+radius), outline=1, width=2)
        frames.append(frame)
    frames[0].save(destination / "Raster pulse.gif", save_all=True, append_images=frames[1:], duration=80, loop=0, disposal=2, transparency=0)

    def variable(value):
        result = [value & 127]
        while value > 127:
            value >>= 7
            result.insert(0, 128 | (value & 127))
        return bytes(result)

    events = bytearray(b"\x00\xff\x51\x03\x07\xa1\x20")
    for pitch in (62, 69, 65, 72, 69, 62, 65, 60):
        events += b"\x00\x90" + bytes((pitch, 96))
        events += variable(360) + b"\x80" + bytes((pitch, 0))
        events += variable(120) + b"\xff\x01\x00"
    events += b"\x00\xff\x2f\x00"
    (destination / "Pulse phrase.mid").write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + b"MTrk" + struct.pack(">I", len(events)) + events)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    create_assets(parser.parse_args().output)
