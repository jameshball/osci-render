#!/usr/bin/env python3
"""A dropped video's Prepare video sheet shows one of its frames, traced as the settings will trace it."""
import json
import subprocess
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-video-preview")
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


import os
import shutil
import time
from pathlib import Path

# White cells on black, so the outlines have something to follow.
video = os.environ.get("MOTION_PREVIEW_VIDEO")
if not video:
    ffmpeg = shutil.which("ffmpeg") or str(Path.home() / "Library/Application Support/osci-render/ffmpeg")
    video = str(session.artifact_dir / "life.mp4")
    subprocess.run([ffmpeg, "-loglevel", "error", "-y", "-f", "lavfi", "-i", "life=s=160x120:mold=10:ratio=0.12:death_color=black:life_color=white,scale=480:360:flags=neighbor",
        "-t", "4", "-pix_fmt", "yuv420p", video], check=True)


def caption():
    for node in nodes(snapshot()):
        if "Source preview" in (node.get("name"), node.get("title"), node.get("componentName")):
            return node
    raise RuntimeError("The Prepare video sheet should show a preview")


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("resize workspace", "resize-window", "--w", 1440, "--h", 900)
    step("drop the video", "drop-files", "--file", video, "--class", "MotionEditor", "--exact")
    # Without FFmpeg it asks to download it first, then shows the sheet.
    if os.environ.get("MOTION_PREVIEW_DOWNLOAD"):
        command("wait-for-locator", "--name", "Yes", "--exact", "--timeout-ms", "10000")
        step("download FFmpeg", "click", "--name", "Yes", "--exact")
        time.sleep(3)
        step("downloading screenshot", "screenshot", "--file", session.artifact_dir / "downloading.png", "--scale", "2")
    command("wait-for-locator", "--name", "Prepare video", "--exact", "--timeout-ms", "240000")
    time.sleep(4)
    step("outlines screenshot", "screenshot", "--file", session.artifact_dir / "outlines.png", "--scale", "2")
    caption()
    step("Hilbert", "set-value", "--name", "Image trace mode", "--exact", "Hilbert")
    time.sleep(4)
    step("hilbert screenshot", "screenshot", "--file", session.artifact_dir / "hilbert.png", "--scale", "2")
    step("Scanlines", "set-value", "--name", "Image trace mode", "--exact", "Scanlines")
    time.sleep(3)
    step("scanlines screenshot", "screenshot", "--file", session.artifact_dir / "scanlines.png", "--scale", "2")
    print("Motion video preview passed", flush=True)
finally:
    session.stop_app()
