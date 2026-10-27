#!/usr/bin/env python3
"""Export a project's beam as XYZ from the app and check the file it writes.

Three float channels at the project's rate for its whole length: X and Y in
the output's range, Z the beam's brightness from 0 (dark, its jumps) to 1.
"""
import array
import struct
import subprocess
import time
from jucewright_osci_browser.cli import parse_args
from jucewright_osci_browser.session import BrowserSession

session = BrowserSession(parse_args())
if session.build_app_requested:
    session.build_app()
if not session.find_jucewright():
    session.build_jucewright()
destination = session.artifact_dir / "exported-xyz.wav"
destination.unlink(missing_ok=True)
keep = session.keep_app
session.keep_app = False
# A native save panel can't be driven: the app writes where automation says.
session.launch_app("motion-export-audio", env={"OSCI_MOTION_EXPORT_FILE": str(destination)})
session.keep_app = keep


def command(*args):
    return subprocess.run(session.cli(*args), capture_output=True, text=True, check=True).stdout


def step(label, *args):
    if not session.run_step(label, session.cli(*args)):
        raise RuntimeError(label)


def read_wav(path):
    blob = path.read_bytes()
    chunks, at = {}, 12
    while at + 8 <= len(blob):
        tag, size = struct.unpack("<4sI", blob[at:at + 8])
        chunks[tag] = blob[at + 8:at + 8 + size]
        at += 8 + size + (size & 1)
    tag, channels, rate = struct.unpack("<HHI", chunks[b"fmt "][0:8])
    bits = struct.unpack("<H", chunks[b"fmt "][14:16])[0]
    samples = array.array("f")
    samples.frombytes(chunks[b"data"])
    return channels, rate, bits, samples


try:
    command("wait-for-locator", "--class", "MotionEditor", "--exact")
    step("open example", "drop-files", "--file", session.root_dir / "Resources/motion/examples/Orbit.osci-motion", "--class", "MotionEditor", "--exact")
    command("wait-for-value", "--component-name", "Composition duration", "--value", "16.000s", "--timeout-ms", 60000)
    step("open file menu", "click", "--name", "File", "--exact", "--role", "menuItem")
    step("open export audio", "click", "--name", "Export audio", "--exact")
    step("export beam xyz", "click", "--name", "Beam XYZ (3 ch)...", "--exact")
    for _ in range(180):
        if destination.exists() and destination.stat().st_size > 0:
            break
        time.sleep(1)
    else:
        raise RuntimeError("The export never wrote its file")
    time.sleep(2)
    channels, rate, bits, samples = read_wav(destination)
    frames = len(samples) // channels
    if channels != 3 or bits != 32 or rate <= 0:
        raise RuntimeError(f"Expected 3 float channels, got {channels} channels of {bits} bits at {rate} Hz")
    if abs(frames / rate - 16.0) > 1.0 / rate:
        raise RuntimeError(f"Expected 16 s, got {frames / rate:.4f} s")
    z = samples[2::3]
    dark = sum(value <= 0.02 for value in z) / len(z)
    if min(z) < 0 or max(z) > 1 or not 0.01 < dark < 0.5:
        raise RuntimeError(f"Z out of range or implausible: {min(z)}..{max(z)}, {dark:.1%} dark")
    step("export screenshot", "screenshot", "--file", session.artifact_dir / "export-audio.png")
    print(f"Motion export-audio smoke passed: {frames} frames at {rate} Hz, {dark:.1%} dark")
finally:
    if not keep:
        session.stop_app()
