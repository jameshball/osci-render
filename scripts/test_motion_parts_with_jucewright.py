#!/usr/bin/env python3
"""Pick parts of a vector source in the Scene and extract them as their own clip."""
import json
import re
import subprocess
from PIL import Image
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-parts")
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def snapshot():
    return json.loads(command("snapshot", "--json", "--full"))


def names():
    return {node.get("name", "") for node in nodes(snapshot())}


def part_count():
    for name in names():
        found = re.fullmatch(r"(\d+) parts?", name)
        if found:
            return int(found.group(1))
    return 0


def picked_pixels(path, left, right, top, bottom):
    image = Image.open(path).convert("RGB")
    scale = image.width / window["w"]
    count = 0
    for x in range(int(left * scale), int(right * scale)):
        for y in range(int(top * scale), int(bottom * scale)):
            r, g, b = image.getpixel((x, y))
            # The selection colour: pale green, unlike the neutral grey of unpicked parts.
            if g > 200 and r > 110 and b > 130 and g - r > 50:
                count += 1
    return count


source = session.artifact_dir / "Two squares.svg"
source.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100">'
                  '<path fill="none" stroke="white" d="M10 10 H70 V90 H10 Z M130 10 H190 V90 H130 Z"/>'
                  '</svg>')
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("import two squares", "drop-files", "--file", source, "--class", "MotionEditor", "--exact")
    session.wait_for_undo("Import object")
    tree = snapshot()
    window = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionEditor")
    scene = next(n["bounds"] for n in nodes(tree) if n.get("class") == "MotionCompositionView")
    middle = scene["x"] + scene["w"] // 2
    step("pick parts mode", "press", "Tab", "--class", "MotionCompositionView", "--exact")
    # From inside the Scene (clear of its tool strip) out to its left edge.
    step("drag over the left square", "drag-xy", middle - 4, scene["y"] + scene["h"] - 20, scene["x"] + 2, scene["y"] + 20, "--steps", 8)
    picked = part_count()
    if picked < 4:
        raise RuntimeError(f"The left square's parts should be picked (got {picked})")
    shot = session.artifact_dir / "picked.png"
    command("screenshot", "--file", shot)
    left = picked_pixels(shot, scene["x"] + 2, middle - 4, scene["y"] + 20, scene["y"] + scene["h"] - 20)
    right = picked_pixels(shot, middle + 4, scene["x"] + scene["w"] - 60, scene["y"] + 100, scene["y"] + scene["h"] - 20)
    if left < 20 or right > 0:
        raise RuntimeError(f"Picked parts should be highlighted only on the left (left {left}, right {right})")
    step("extract the left square", "click", "--name", "Extract parts", "--exact")
    session.wait_for_undo("Extract parts")
    after = names()
    if "Two squares part.svg" not in after or "Two squares rest.svg" not in after:
        raise RuntimeError("Extraction should leave a part clip and a rest clip")
    if part_count() != 0:
        raise RuntimeError("Extraction should leave part mode")
    step("undo extraction", "click", "--name", "Undo", "--exact")
    session.wait_for_undo("Import object")
    if "Two squares part.svg" in names():
        raise RuntimeError("Undo should restore the single source")
    # Escape clears the picked parts, then leaves part mode.
    step("pick parts again", "press", "Tab", "--class", "MotionCompositionView", "--exact")
    step("drag over the left square again", "drag-xy", middle, scene["y"] + scene["h"] - 20, scene["x"] + 2, scene["y"] + 20, "--steps", 8)
    step("drag over the right square instead", "drag-xy", middle, scene["y"] + scene["h"] - 20, scene["x"] + scene["w"] - 2, scene["y"] + 100, "--steps", 8)
    if part_count() != picked:
        raise RuntimeError("A second drag replaces the picks unless Shift is held")
    # Pieces: a thin box across the left square takes the whole square.
    step("pick whole pieces", "click", "--name", "Pick pieces", "--exact")
    centre_y = scene["y"] + scene["h"] // 2
    step("drag across the left square", "drag-xy", middle - 4, centre_y, scene["x"] + 2, centre_y + 6, "--steps", 6)
    if part_count() != picked:
        raise RuntimeError(f"Pieces should take the whole square ({part_count()} of {picked})")
    step("back to box picking", "click", "--name", "Box pick", "--exact")
    step("clear the picked parts", "press", "Escape", "--class", "MotionCompositionView", "--exact")
    if part_count() != 0:
        raise RuntimeError("Escape should clear the picked parts")
    step("leave part mode", "press", "Escape", "--class", "MotionCompositionView", "--exact")
    print("Motion parts workflow passed", flush=True)
finally:
    session.stop_app()
