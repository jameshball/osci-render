#!/usr/bin/env python3
"""Exercise raster preparation, GIF import and undoable source settings."""
import json
import subprocess
from PIL import Image, ImageDraw
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-images")
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


def wait_undo(label):
    command("wait-for-locator", "--name", "Undo " + label, "--role", "label", "--exact")


def combo_value(name):
    tree = json.loads(command("snapshot", "--json", "--full"))
    return next(node["value"] for node in nodes(tree) if node.get("componentName") == name and "value" in node)


png = session.artifact_dir / "Signal.png"
image = Image.new("RGBA", (192, 128), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.polygon([(18, 100), (18, 28), (64, 64), (64, 100)], fill=(40, 225, 255, 255))
draw.polygon([(74, 100), (74, 64), (120, 28), (120, 100)], fill=(255, 70, 190, 255))
draw.rectangle((140, 28, 174, 100), fill=(255, 195, 50, 255))
image.save(png)
gif = session.artifact_dir / "Pulse.gif"
frames = []
for radius, colour in [(18, (255, 60, 40)), (27, (40, 255, 90)), (36, (40, 100, 255))]:
    frame = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
    ImageDraw.Draw(frame).ellipse((48-radius, 48-radius, 48+radius, 48+radius), outline=colour + (255,), width=4)
    frames.append(frame)
frames[0].save(gif, save_all=True, append_images=frames[1:], duration=[100, 300, 200], loop=0, disposal=2, transparency=0)

try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("import PNG", "drop-files", "--file", png, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
    command("wait", "--ms", 400)
    assert combo_value("Image trace mode") == "Outlines"
    step("image settings screenshot", "screenshot", "--file", session.artifact_dir / "settings.png")
    step("prepare outlines", "click", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
    wait_undo("Import object")
    command("wait", "--ms", 500)
    step("colored outlines screenshot", "screenshot", "--file", session.artifact_dir / "outlines.png")
    step("open image settings", "click", "--name", "Image settings...", "--exact")
    step("choose scanlines", "set-value", "--name", "Image trace mode", "--exact", "Scanlines")
    assert combo_value("Image trace mode") == "Scanlines"
    assert combo_value("Image detail") == "64 px"
    step("choose explicit image detail", "select-option", "--name", "Image detail", "--class", "juce::ComboBox", "--exact", "--text", "128 px")
    command("set-value", "--name", "Image trace mode", "--exact", "Outlines")
    command("set-value", "--name", "Image trace mode", "--exact", "Scanlines")
    assert combo_value("Image detail") == "128 px"
    step("restore scanline detail", "select-option", "--name", "Image detail", "--class", "juce::ComboBox", "--exact", "--text", "64 px")
    step("prepare scanlines", "click", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
    wait_undo("Rebuild source cache")
    command("wait", "--ms", 500)
    step("scanline screenshot", "screenshot", "--file", session.artifact_dir / "scanlines.png")
    step("undo image preparation", "click", "--name", "Undo", "--exact")
    step("inspect restored image settings", "click", "--name", "Image settings...", "--exact")
    assert combo_value("Image trace mode") == "Outlines"
    step("close image settings", "press", "Escape", "--class", "MotionRasterSettingsPanel", "--exact")
    command("wait", "--ms", 600)
    step("import GIF", "drop-files", "--file", gif, "--class", "MotionEditor", "--exact")
    command("wait-for-locator", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
    step("prepare GIF", "click", "--name", "Prepare image", "--class", "juce::TextButton", "--exact")
    command("wait-for-locator", "--name", "Pulse.gif", "--role", "label", "--exact")
    command("wait", "--ms", 500)
    step("combined image workspace", "screenshot", "--file", session.artifact_dir / "workspace.png")
    step("compact workspace", "resize-window", "--w", 1100, "--h", 700)
    step("compact image workspace screenshot", "screenshot", "--file", session.artifact_dir / "compact.png")
    print("Motion image workflow passed", flush=True)
finally:
    session.stop_app()
