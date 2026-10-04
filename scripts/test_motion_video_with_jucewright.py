#!/usr/bin/env python3
"""Import, edit, undo and reopen baked MP4/MOV sources through Motion.

Generate inputs first with generate_motion_video_fixtures.py.
"""
import os
import shutil
import sys
import tempfile
import struct
import subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
keep = session.keep_app
session.keep_app = False
session.launch_app("motion-video")
session.keep_app = keep
fixtures = Path(os.environ.get("MOTION_VIDEO_FIXTURES", Path(tempfile.gettempdir()) / "motion-video-fixtures"))


def ensure_fixtures():
    """Generates the MP4/MOV inputs when they are missing (temp folders get cleaned)."""
    if all((fixtures / name).is_file() for name in ("motion.mp4", "motion.mov")):
        return
    home = Path.home()
    candidates = [shutil.which("ffmpeg"), home / "Library/Application Support/osci-render/ffmpeg",
                  home / ".config/osci-render/ffmpeg", home / "AppData/Roaming/osci-render/ffmpeg.exe"]
    ffmpeg = next((str(candidate) for candidate in candidates if candidate and Path(candidate).is_file()), None)
    if ffmpeg is None:
        raise SystemExit("Video fixtures are missing and no FFmpeg was found to generate them.")
    subprocess.run([sys.executable, str(session.root_dir / "scripts" / "generate_motion_video_fixtures.py"), "--output-dir", str(fixtures), "--ffmpeg", ffmpeg], check=True)


ensure_fixtures()


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def wait_undo(label):
    command("wait-for-locator", "--name", "Undo " + label, "--role", "label", "--exact", "--timeout-ms", "30000")


def save():
    step("save video study", "press", "command + s", "--class", "MotionEditor")
    data = project.read_bytes()
    return ET.fromstring(data[8:8 + struct.unpack("<I", data[4:8])[0]]).find("composition")


root = ET.Element("motion-project", schema="1")
ET.SubElement(root, "composition", name="Video study", duration="8", bpm="120", fps="30")
xml = ET.tostring(root, encoding="utf-8")
project = session.artifact_dir / "video.osci-motion"
project.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Video study", "--timeout-ms", "10000")
    step("size workspace", "resize-window", "--w", "1440", "--h", "900")
    step("drop MP4", "drop-files", "--file", fixtures / "motion.mp4", "--class", "MotionEditor", "--exact")
    step("cancel initial settings", "click", "--name", "Close icon", "--exact")
    assert not save().findall("asset")
    for name in ("motion.mp4", "motion.mov"):
        step("drop " + name, "drop-files", "--file", fixtures / name, "--class", "MotionEditor", "--exact")
        step("set video bake rate", "set-value", "--name", "Video bake frame rate", "--role", "slider", "24")
        step("choose tracing detail", "select-option", "--name", "Image detail", "--class", "juce::ComboBox", "--exact", "--text", "64 px")
        step("choose sample count", "select-option", "--name", "Image samples per frame", "--class", "juce::ComboBox", "--exact", "--text", "1024")
        step("video settings", "screenshot", "--file", session.artifact_dir / (name + "-settings.png"))
        step("prepare " + name, "click", "--name", "Prepare video", "--class", "juce::TextButton", "--exact")
        # Generous: a Debug build preparing video in a VM takes far longer than on a Mac.
        command("wait-for-locator", "--name", name, "--role", "label", "--exact", "--timeout-ms", "120000")
    state = save()
    assert len(state.findall("asset")) == 2
    assert all(float(clip.get("duration")) == 2 for clip in state.findall("track/clip"))
    assert all(asset.find("video-cache") is not None for asset in state.findall("asset"))
    step("early video frame", "click", "--class", "MotionTimelineView", "--position", "240,12")
    command("wait", "--ms", "500")
    step("early frame screenshot", "screenshot", "--file", session.artifact_dir / "early-video.png")
    step("late video frame", "click", "--class", "MotionTimelineView", "--position", "340,12")
    command("wait", "--ms", "500")
    step("late frame screenshot", "screenshot", "--file", session.artifact_dir / "late-video.png")
    step("edit video source", "click", "--name", "Video settings...", "--exact")
    step("change bake rate", "set-value", "--name", "Video bake frame rate", "--role", "slider", "12")
    step("rebuild video source", "click", "--name", "Prepare video", "--exact")
    wait_undo("Rebuild source cache")
    state = save()
    assert float(state.findall("asset")[-1].find("raster").get("frameRate")) == 12
    step("undo video preparation", "click", "--name", "Undo", "--exact")
    assert float(save().findall("asset")[-1].find("raster").get("frameRate")) == 24
    step("redo video preparation", "click", "--name", "Redo", "--exact")
    saved = ET.tostring(save())
    session.stop_app()
    session.keep_app = False
    session.launch_app("video-reopened")
    session.keep_app = keep
    session.open_project(project)
    command("wait-for-value", "--component-name", "Composition name", "--hidden", "--value", "Video study", "--timeout-ms", "30000")
    assert ET.tostring(save()) == saved
    step("reopened video project", "screenshot", "--file", session.artifact_dir / "video-reopened.png")
    step("import corrupt video", "drop-files", "--file", fixtures / "corrupt.mp4", "--class", "MotionEditor", "--exact")
    step("prepare corrupt video", "click", "--name", "Prepare video", "--exact")
    command("wait-for-locator", "--name", "Cannot decode this video. Check that it contains a supported, undamaged video stream.", "--role", "label", "--exact", "--timeout-ms", "30000")
    assert ET.tostring(save()) == saved
    step("video failure message", "screenshot", "--file", session.artifact_dir / "video-failure.png")
    step("import single frame", "drop-files", "--file", fixtures / "single-frame.mp4", "--class", "MotionEditor", "--exact")
    step("match single frame rate", "set-value", "--name", "Video bake frame rate", "--role", "slider", "24")
    step("prepare single frame", "click", "--name", "Prepare video", "--exact")
    command("wait-for-locator", "--name", "single-frame.mp4", "--role", "label", "--exact", "--timeout-ms", "30000")
    assert abs(float(save().findall("track/clip")[-1].get("duration")) - 1 / 24) < 1e-12
    step("compact video workspace", "resize-window", "--w", "1100", "--h", "740")
    step("compact video settings", "click", "--name", "Video settings...", "--exact")
    command("wait-for-locator", "--name", "Prepare video", "--class", "juce::TextButton", "--exact")
    command("wait", "--ms", "500")
    step("compact settings screenshot", "screenshot", "--file", session.artifact_dir / "compact-video-settings.png")
    print("MP4/MOV preparation, source edit, undo/redo and archived reopen passed", flush=True)
finally:
    session.stop_app()
