#!/usr/bin/env python3
"""Generate original varied DAH demo source assets, never a project arrangement.

Dependencies: Python 3 and Pillow. All geometry/animation is generated here;
there are no downloaded images, fonts, music or other external source assets.
"""
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

TAU = math.tau


def rotate(point, yaw=0.0, pitch=0.0, roll=0.0):
    x, y, z = point
    x, z = x * math.cos(yaw) + z * math.sin(yaw), -x * math.sin(yaw) + z * math.cos(yaw)
    y, z = y * math.cos(pitch) - z * math.sin(pitch), y * math.sin(pitch) + z * math.cos(pitch)
    return (x * math.cos(roll) - y * math.sin(roll), x * math.sin(roll) + y * math.cos(roll), z)


def static(value):
    return {"a": 0, "k": value}


def shape_path(vertices, closed=False):
    return {"i": [[0, 0] for _ in vertices], "o": [[0, 0] for _ in vertices],
            "v": [[round(x, 4), round(y, 4)] for x, y in vertices], "c": closed}


def animated_path(evaluate, closed=False, frame_count=120, spacing=10):
    # Endpoint repeats frame zero. All morph keys retain identical vertex counts.
    frames = list(range(0, frame_count + 1, spacing))
    paths = [shape_path(evaluate(frame / frame_count), closed) for frame in frames]
    keys = []
    for index, frame in enumerate(frames):
        key = {"t": frame, "s": [paths[index]]}
        if index + 1 < len(frames):
            key.update({"e": [paths[index + 1]], "i": {"x": 0.667, "y": 0.667},
                        "o": {"x": 0.333, "y": 0.333}})
        keys.append(key)
    return {"a": 1, "k": keys}


def layer(index, name, path, colour, frame_count=120):
    return {"ddd": 0, "ind": index, "ty": 4, "nm": name, "sr": 1,
            "ks": {"o": static(100), "r": static(0), "p": static([128, 128, 0]),
                   "a": static([0, 0, 0]), "s": static([100, 100, 100])},
            "ao": 0, "shapes": [{"ty": "sh", "nm": name, "ks": path},
                                     {"ty": "st", "nm": "Neon outline", "c": static([*colour, 1]),
                                      "o": static(100), "w": static(2), "lc": 2, "lj": 2, "ml": 4, "bm": 0}],
            "ip": 0, "op": frame_count, "st": 0, "bm": 0}


def create_assets(destination):
    destination.mkdir(parents=True, exist_ok=True)
    manifest = {"collection": "DAH / kinetic matter", "original": True,
                "purpose": "Source media for the revised 185.6-second music-led demo; no arrangement included.",
                "assets": []}
    previews = []

    def obj(name, paths, description):
        vertices, indexed, unique_edges = {}, [], set()
        for path in paths:
            indices = []
            for point in path:
                point = tuple(round(value, 8) for value in point)
                assert all(math.isfinite(value) for value in point)
                if point not in vertices:
                    vertices[point] = len(vertices) + 1
                indices.append(vertices[point])
            assert len(indices) >= 2
            for a, b in zip(indices, indices[1:]):
                assert a != b
                unique_edges.add(tuple(sorted((a, b))))
            indexed.append(indices)
        assert len(unique_edges) <= 100
        assert all(max(point[axis] for point in vertices) - min(point[axis] for point in vertices) > 0.25 for axis in range(3))
        lines = ["# Original DAH asset; explicit wire edges, no triangulation diagonals."]
        lines += [f"v {x:.8f} {y:.8f} {z:.8f}" for x, y, z in vertices]
        lines += ["l " + " ".join(map(str, indices)) for indices in indexed]
        (destination / name).write_text("\n".join(lines) + "\n")
        manifest["assets"].append({"file": name, "format": "OBJ", "vertices": len(vertices),
                                   "edges": len(unique_edges), "description": description})
        preview = Image.new("RGB", (256, 256), (7, 11, 16))
        draw = ImageDraw.Draw(preview)
        for path in paths:
            screen = []
            for point in path:
                x, y, z = rotate(point, .43, -.24, .05)
                perspective = 3.8 / (3.8 - z)
                screen.append((128 + x * 87 * perspective, 128 - y * 87 * perspective))
            draw.line(screen, fill=(110, 250, 202), width=2)
        previews.append((name, preview))

    # Two opposed, tapered helixes with a sparse ladder joining them: 57 edges.
    helices = []
    for side in (0, math.pi):
        helix = []
        for index in range(25):
            t = index / 24
            angle = TAU * 1.15 * t + side
            radius = .48 + .12 * math.sin(math.pi * t)
            helix.append((radius * math.cos(angle), (t - .5) * 1.85, radius * math.sin(angle)))
        helices.append(helix)
    cage = helices + [[helices[0][index], helices[1][index]] for index in range(0, 25, 3)]
    obj("01 Helix ladder.obj", cage, "True 3D double helix with sparse rungs; rotates into a DNA-like ladder, not a flat ring.")

    # Spacecraft: asymmetric depth of cockpit, keel and rear engines creates
    # recognisable changes under both yaw and roll, with only silhouette edges.
    nose = (0, .98, .1)
    shoulder_left, shoulder_right = (-.34, .08, .16), (.34, .08, .16)
    wing_left, wing_right = (-.94, -.62, -.08), (.94, -.62, -.08)
    tail_left, tail_right = (-.22, -.44, -.2), (.22, -.44, -.2)
    canopy_front, canopy_back = (0, .38, .52), (0, -.24, .34)
    keel = (0, -.3, -.52)
    ship = [[nose, shoulder_left, wing_left, tail_left, tail_right, wing_right, shoulder_right, nose],
            [nose, canopy_front, canopy_back, tail_left], [canopy_back, tail_right],
            [shoulder_left, canopy_front, shoulder_right], [wing_left, keel, wing_right],
            [nose, keel, tail_left], [keel, tail_right]]
    for x in (-.18, .18):
        ship.append([(x - .07, -.44, -.16), (x - .07, -.79, -.13),
                     (x + .07, -.79, -.13), (x + .07, -.44, -.16)])
    obj("02 Faceted glider.obj", ship, "Angular spacecraft with raised canopy, swept wings, deep keel and twin exhaust rails.")

    # Four tetrahedral counterweights hung from a tilted spatial axle.
    sculpture = [[(-.8, -.72, -.36), (.83, .74, .38)]]
    tetra = [(0, .23, 0), (-.2, -.12, -.15), (.2, -.12, -.15), (0, -.12, .22)]
    for index in range(4):
        t = index / 3
        centre = (-.7 + 1.4 * t, -.65 + 1.3 * t, -.3 + .6 * t)
        shape = [rotate(point, index * .8, .3, index * .55) for point in tetra]
        shape = [tuple(a + b for a, b in zip(point, centre)) for point in shape]
        sculpture.extend([[shape[a], shape[b]] for a in range(4) for b in range(a + 1, 4)])
    sculpture += [[(-.86, -.82, -.36), (-.45, -.82, -.36), (-.65, -.82, .03), (-.86, -.82, -.36)],
                  [(.58, .83, .35), (.96, .83, .35), (.78, .83, .72), (.58, .83, .35)]]
    obj("03 Counterweight mobile.obj", sculpture, "Four faceted tetrahedral weights on a diagonal axle; separate solids read as a kinetic sculpture.")

    # Accordion surface: rectilinear silhouette, staggered folds in depth.
    folds = []
    columns = []
    for index in range(7):
        x = -.9 + index * .3
        z = .32 if index % 2 else -.32
        columns.append([(x, -.64, z), (x, .64, z)])
        folds.append(columns[-1])
    folds += [[column[end] for column in columns] for end in (0, 1)]
    folds += [[(x, 0, z) for x, _, z in [column[0] for column in columns]]]
    obj("04 Folded signal wall.obj", folds, "A low-edge accordion sheet; yaw reveals deep alternating folds, frontal view forms graphic vertical bars.")

    # Open cuboid staircase: one continuous ascending spatial ribbon.
    stair = []
    for index in range(5):
        x, y, z = -.8 + index * .32, -.7 + index * .29, -.48 + index * .21
        stair.append([(x, y, z), (x + .29, y, z), (x + .29, y, z + .34), (x, y, z + .34), (x, y, z)])
        if index < 4:
            stair.append([(x + .29, y, z), (x + .32, y + .29, z + .21)])
            stair.append([(x + .29, y, z + .34), (x + .32, y + .29, z + .55)])
    obj("05 Impossible steps.obj", stair, "Five open rectangular treads climbing in all three dimensions; architectural rather than radial.")

    def lottie(name, layers, description):
        document = {"v": "5.7.4", "fr": 30, "ip": 0, "op": 120, "w": 256, "h": 256,
                    "nm": name[:-5], "ddd": 0, "assets": [], "layers": layers}
        (destination / name).write_text(json.dumps(document, separators=(",", ":")) + "\n")
        manifest["assets"].append({"file": name, "format": "Lottie JSON", "frames": 120, "fps": 30,
                                   "seconds": 4, "description": description})
        preview = Image.new("RGB", (256, 256), (7, 11, 16))
        draw = ImageDraw.Draw(preview)
        for item in layers:
            path = item["shapes"][0]["ks"]
            path = path["k"][3]["s"][0] if path["a"] else path["k"]
            vertices = [(128 + x, 128 + y) for x, y in path["v"]]
            if path["c"]:
                vertices.append(vertices[0])
            colour = tuple(round(value * 255) for value in item["shapes"][1]["c"]["k"][:3])
            draw.line(vertices, fill=colour, width=2)
        previews.append((name, preview))

    wings = [layer(1, "Central spine", static(shape_path([(0, -63), (10, 0), (0, 64), (-10, 0)], True)), (.95, .76, .3))]
    for side in (-1, 1):
        for blade in range(3):
            def evaluate(t, side=side, blade=blade):
                opening = .5 + .5 * math.sin(TAU * t - blade * .7)
                root_y = -36 + blade * 35
                extent = 40 + 55 * opening
                rise = -48 + blade * 17 + 29 * math.cos(TAU * t - blade * .7)
                return [(side * 9, root_y), (side * extent, root_y + rise),
                        (side * (extent - 13), root_y + rise + 23), (side * 12, root_y + 14)]
            wings.append(layer(len(wings) + 1, f"{'Left' if side < 0 else 'Right'} blade {blade + 1}",
                               animated_path(evaluate, True), (.25, .88, 1) if blade % 2 == 0 else (1, .42, .3)))
    lottie("06 Mechanical wings.json", wings, "Six quadrilateral blades unfold and fold with staggered motion around an amber spine; genuine path-morph animation.")

    loom = []
    for strand in range(5):
        def evaluate(t, strand=strand):
            return [(-100 + point * 25,
                     -62 + strand * 31 + 16 * math.sin(TAU * (t + point / 8) + strand * .7)) for point in range(9)]
        loom.append(layer(strand + 1, f"Travelling strand {strand + 1}", animated_path(evaluate),
                          (.28, 1, .63) if strand % 2 else (.63, .48, 1)))
    lottie("07 Travelling signal loom.json", loom, "Five angular waveform ribbons travel horizontally at distinct phases; a flowing typographic/oscilloscope texture.")

    def gif(name, count, draw_frame, description):
        frames = []
        palette = [0, 0, 0, 150, 255, 205, 255, 164, 83, 110, 202, 255] + [0, 0, 0] * 252
        for index in range(count):
            image = Image.new("P", (160, 160), 0)
            image.putpalette(palette)
            draw_frame(ImageDraw.Draw(image), index / count)
            frames.append(image)
        frames[0].save(destination / name, save_all=True, append_images=frames[1:],
                       duration=80, loop=0, disposal=2, transparency=0, optimize=False)
        manifest["assets"].append({"file": name, "format": "Animated GIF", "frames": count,
                                   "frame_delay_ms": 80, "seconds": count * .08, "width": 160, "height": 160,
                                   "description": description})
        previews.append((name, frames[count // 5].convert("RGBA").convert("RGB").resize((256, 256))))

    def walker(draw, t):
        phase = TAU * t
        bob = 3 * math.cos(phase * 2)
        hip = (78, 94 + bob)
        shoulder = (83, 61 + bob)
        # Far limbs, then body, then near limbs. Broad shapes make distinct
        # contours after raster tracing instead of dense pixel-sized wire noise.
        for side, colour in ((-1, 3), (1, 1)):
            swing = math.sin(phase) * side
            knee = (hip[0] + 18 * swing, 114 - 5 * abs(swing) + bob)
            ankle = (hip[0] + 31 * swing, 137 - 12 * max(0, -swing))
            draw.line([hip, knee, ankle, (ankle[0] + 12, ankle[1])], fill=colour, width=6)
            elbow = (shoulder[0] - 18 * swing, 80 + bob)
            hand = (shoulder[0] - 30 * swing, 91 - 12 * abs(swing) + bob)
            draw.line([shoulder, elbow, hand], fill=colour, width=5)
        draw.polygon([(74, 61 + bob), (91, 62 + bob), (86, 88 + bob), (78, 97 + bob), (70, 88 + bob)], fill=2)
        draw.polygon([(77, 36 + bob), (93, 38 + bob), (98, 48 + bob), (90, 58 + bob), (77, 55 + bob), (72, 44 + bob)], fill=1)
        draw.line([(35, 144), (126, 144)], fill=3, width=2)
    gif("08 Geometric night walker.gif", 32, walker, "A recognisable articulated walking figure with polygonal head, counter-swinging arms and stepping legs; full stride loop.")

    def bird(draw, t):
        phase = TAU * t
        lift = math.sin(phase)
        middle = 76 + 4 * math.cos(phase * 2)
        for side in (-1, 1):
            wing_tip = (80 + side * 62, middle - 39 * lift)
            joint = (80 + side * 29, middle - 20 * lift - 7)
            draw.polygon([(80 + side * 7, middle), joint, wing_tip,
                          (80 + side * 37, middle + 13 - 16 * lift)], fill=1 if side < 0 else 3)
        draw.polygon([(80, middle - 21), (90, middle + 9), (80, middle + 28), (70, middle + 9)], fill=2)
        draw.polygon([(80, middle + 23), (96, middle + 49), (80, middle + 42), (64, middle + 49)], fill=3)
        draw.polygon([(80, middle - 19), (75, middle - 27), (85, middle - 27)], fill=1)
    gif("09 Origami flight.gif", 32, bird, "A front-facing angular bird silhouette changes between steep raised wings and a flat gliding span; no circular motif.")

    # Preserve the authoring task's existing Lua/text sources byte-for-byte.
    # Stateful curves use play_time and are intended for Motion's Lua bake UI.
    companion_sources = {
        'Electric braid.lua': 'local t=play_time; local p=phase; local r=0.5+0.15*math.sin(3*p+2*math.pi*t/3.2); return {r*math.cos(2*p),r*math.sin(3*p),0.35*math.cos(p+2*math.pi*t/3.2),0.2,0.8,1}\n',
        'Folded wave.lua': 'local t=play_time; local p=phase; return {0.75*math.cos(p),0.25*math.sin(5*p+2*math.pi*t/3.2),0.45*math.sin(2*p-2*math.pi*t/3.2),1,0.3,0.65}\n',
        'DAH.txt': 'DAH!',
        'DO.txt': 'DO',
        'DAH DAH.txt': 'DAH DAH',
    }
    for name, text in companion_sources.items():
        (destination / name).write_text(text)
        manifest["assets"].append({"file": name, "format": "Lua" if name.endswith(".lua") else "Text",
                                   "description": "Existing authoring source reproduced verbatim; Lua motion has a 3.2-second cycle." if name.endswith(".lua")
                                   else "Original title/accent text reproduced verbatim."})

    # Lightweight format checks; import/render validation is intentionally left
    # to Motion's shared decoders, not falsely implied by these structural checks.
    for asset in manifest["assets"]:
        path = destination / asset["file"]
        asset["bytes"] = path.stat().st_size
        if asset["format"] == "OBJ":
            lines = path.read_text().splitlines()
            vertices = [line for line in lines if line.startswith("v ")]
            for line in lines:
                if line.startswith("l "):
                    assert all(1 <= int(index) <= len(vertices) for index in line.split()[1:])
        elif asset["format"] == "Lottie JSON":
            document = json.loads(path.read_text())
            assert document["op"] == 120 and document["fr"] == 30
            for item in document["layers"]:
                shape = item["shapes"][0]["ks"]
                if shape["a"]:
                    assert shape["k"][0]["t"] == 0 and shape["k"][-1]["t"] == 120
                    assert shape["k"][0]["s"] == shape["k"][-1]["s"]
                    assert len({len(key["s"][0]["v"]) for key in shape["k"]}) == 1
        elif asset["format"] == "Animated GIF":
            with Image.open(path) as image:
                assert image.n_frames == asset["frames"] and image.size == (160, 160)
                hashes = set()
                for index in range(image.n_frames):
                    image.seek(index)
                    assert image.info["duration"] == 80
                    hashes.add(image.convert("RGBA").tobytes())
                assert len(hashes) >= image.n_frames // 2
                asset["distinct_frames"] = len(hashes)
    manifest["limitations"] = ["OBJ assets are static 3D wire geometry; animate them with Motion transforms.",
                               "Lottie assets use standard shape paths, strokes and morph keys only; Motion importer playback still needs application validation.",
                               "GIFs are 160x160 silhouette sources and may benefit from outline tracing/downsampling to control beam point count.",
                               "GIF frame delays are 80ms (12.5 FPS); Lottie animation is 30 FPS. These source rates are independent of project tempo.",
                               "No soundtrack, MIDI, project layout, clip arrangement or output-camera animation is generated."]
    (destination / "asset-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    sheet = Image.new("RGB", (3 * 280, 3 * 292), (15, 19, 25))
    draw = ImageDraw.Draw(sheet)
    for index, (name, preview) in enumerate(previews):
        x, y = index % 3 * 280, index // 3 * 292
        sheet.paste(preview, (x + 12, y + 8))
        draw.text((x + 12, y + 268), name, fill=(224, 229, 236))
    sheet.save(destination / "asset-contact-sheet.png")
    print(json.dumps({"output": str(destination.resolve()), "assets": manifest["assets"]}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Directory for original source assets and generated inspection sheet")
    create_assets(parser.parse_args().output)
