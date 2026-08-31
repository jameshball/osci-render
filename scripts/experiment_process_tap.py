#!/usr/bin/env python3
"""Automated SOSCI capture-only and physical/aggregate buffer experiments on macOS.

Requires the opt-in ProcessTapDiagnostics instrumentation and a saved System Audio
setup. Uses disposable profiles, a quiet continuous stereo tone, and reversed run
order. Does not change the user's saved settings. Run with SOSCI closed.
"""

import argparse
from datetime import datetime
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import tempfile
import time
import wave
import xml.etree.ElementTree as ET

CASES = {
    "full-32": (32, False, 0),
    "capture-32": (32, True, 0),
    "full-64": (64, False, 0),
    "capture-64": (64, True, 0),
    "full-32-physical-64": (32, False, 64),
    "capture-32-physical-64": (32, True, 64),
}


def app_pids(executable):
    result = subprocess.run(["pgrep", "-f", "^" + re.escape(str(executable)) + "$"],
                            capture_output=True, text=True, check=False)
    return [int(pid) for pid in result.stdout.split()]


def make_profile(destination, source, frames):
    support = destination / "Library/Application Support"
    support.mkdir(parents=True)
    for name in ("sosci.settings", "sosci_globals.settings", "osci-licensing.settings"):
        if (source / name).exists():
            shutil.copy2(source / name, support / name)
    settings = support / "sosci.settings"
    tree = ET.parse(settings)
    device = tree.find("./VALUE[@name='audioSetup']/DEVICESETUP")
    if device is None or device.get("deviceType") != "Process Audio":
        raise RuntimeError("Save a working System Audio setup in SOSCI before running experiments")
    if not device.get("audioInputDeviceName", "").startswith("System Audio"):
        raise RuntimeError("The saved capture source must be System Audio")
    device.set("audioDeviceBufferSize", str(frames))
    tree.write(settings, encoding="utf-8", xml_declaration=True)
    return support / "osci-render/sosci.log"


def make_tone(path, seconds):
    rate = 48000
    # Quadrature stereo, -40 dBFS: neither channel pair is simultaneously zero.
    block = b"".join(struct.pack("<hh", int(327 * math.sin(2 * math.pi * 220 * i / rate)),
                                int(327 * math.cos(2 * math.pi * 220 * i / rate))) for i in range(rate))
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(rate)
        for _ in range(math.ceil(seconds)):
            output.writeframesraw(block)


def summarise(log, start, end, frames, capture_only, physical_override):
    content = log.read_text()
    if f"ProcessTap diagnostics captureOnly={int(capture_only)}" not in content:
        raise RuntimeError("Missing capture-only mode confirmation")
    rows = []
    for line in content.splitlines():
        match = re.match(r"ProcessTap diagnostics (\d{4}-\S+) (.*)", line)
        if not match:
            continue
        timestamp = datetime.fromisoformat(match[1]).timestamp()
        # Drop boundary summaries to exclude warmup/teardown from measurements.
        if start + 1 <= timestamp <= end:
            row = dict(re.findall(r"(\w+)=([^ ]+)", match[2]))
            rows.append(row)
    if len(rows) < 5:
        raise RuntimeError("Too few diagnostic summaries; check capture permissions/startup")
    total = sum(int(row["callbacks"]) for row in rows)
    if total == 0 or not any(float(row["peak"]) > 0 for row in rows):
        raise RuntimeError("No non-zero captured audio; this is not a valid experiment")
    result = {"requestedFrames": frames, "captureOnly": capture_only, "physicalOverride": physical_override,
              "summarySeconds": len(rows), "callbacks": total}
    for key in ("overBudget", "zeroBlocks", "nullInput", "sizeMismatch", "timestampGaps", "droppedRecords"):
        result[key] = sum(int(row[key]) for row in rows)
    for key in ("maxGapMs", "maxCallbackMs", "longestZeroMs"):
        result[key] = max(float(row[key]) for row in rows)
    result["meanCallbackMs"] = sum(float(row["meanCallbackMs"]) * int(row["callbacks"]) for row in rows) / total
    result["zeroPercent"] = 100 * result["zeroBlocks"] / total
    result["inputFrameRanges"] = sorted({row["inputFrames"] for row in rows})
    result["outputFrameRanges"] = sorted({row["outputFrames"] for row in rows})
    for key in ("configured", "physicalFrames", "aggregateFrames"):
        result[key] = sorted({int(row[key]) for row in rows})
    if physical_override == 0 and (result["configured"] != [frames] or result["aggregateFrames"] != [frames]):
        raise RuntimeError("Baseline device did not honour its requested buffer size")
    if physical_override:
        result["physicalOverrideHonoured"] = result["physicalFrames"] == [physical_override]
        result["independentBuffersAchieved"] = (result["physicalOverrideHonoured"]
            and result["aggregateFrames"] == [frames] and result["inputFrameRanges"] == [f"{frames}..{frames}"])
    if result["droppedRecords"]:
        raise RuntimeError("Diagnostic queue overflowed; results incomplete")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=Path(__file__).resolve().parents[1] / "Builds/sosci/MacOSX/build/Release/sosci.app")
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--warmup", type=int, default=8)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--expect-minimum", type=int, default=0, help="Verify saved smaller requests are clamped to this buffer size")
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    args = parser.parse_args()
    if args.seconds < 10 or args.warmup < 0 or args.repeats < 1:
        parser.error("Use at least 10 seconds, a nonnegative warmup, and at least one repeat")
    executable = args.app / "Contents/MacOS/sosci"
    if app_pids(executable):
        raise RuntimeError("Close SOSCI before starting the experiments")
    artifacts = Path(tempfile.mkdtemp(prefix="sosci-tap-experiments-"))
    print(f"Artifacts: {artifacts}", flush=True)
    source = Path.home() / "Library/Application Support"
    # Snapshot once, so each run starts with exactly the same settings.
    snapshot = artifacts / "settings"
    snapshot.mkdir()
    for name in ("sosci.settings", "sosci_globals.settings", "osci-licensing.settings"):
        if (source / name).exists():
            shutil.copy2(source / name, snapshot / name)
    tone = artifacts / "tone.wav"
    make_tone(tone, args.seconds + args.warmup + 60)
    results = []
    for repeat in range(args.repeats):
        cases = list(args.cases)
        if repeat % 2:
            cases.reverse()
        for case in cases:
            frames, capture_only, physical_override = CASES[case]
            label = f"{repeat + 1}-{case}"
            profile = artifacts / label
            log = make_profile(profile, snapshot, frames)
            player = launcher = None
            pid = None
            print(f"START {label}", flush=True)
            try:
                player = subprocess.Popen(["afplay", str(tone)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                launcher = subprocess.Popen(["open", "-n", "-W", str(args.app),
                    "--env", f"CFFIXED_USER_HOME={profile}", "--env", "OSCI_PROCESS_TAP_DIAGNOSTICS=1",
                    "--env", f"OSCI_PROCESS_TAP_CAPTURE_ONLY={int(capture_only)}",
                    "--env", f"OSCI_PROCESS_TAP_PHYSICAL_FRAMES={physical_override}"])
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    pids = app_pids(executable)
                    if len(pids) == 1:
                        pid = pids[0]
                    if pid is not None and log.exists() and "callbacks=" in log.read_text():
                        break
                    time.sleep(0.5)
                else:
                    raise RuntimeError(f"Capture failed to start at {frames}; inspect {profile}")
                time.sleep(args.warmup)
                start = time.time()
                time.sleep(args.seconds)
                end = time.time()
                if player.poll() is not None:
                    raise RuntimeError("Test tone stopped before measurements finished")
                expected_frames = max(frames, args.expect_minimum)
                result = summarise(log, start, end, expected_frames, capture_only, physical_override)
                if args.expect_minimum and (result["zeroBlocks"] != 0 or result["inputFrameRanges"] != [f"{expected_frames}..{expected_frames}"]):
                    raise RuntimeError("Minimum-buffer regression: silent input or unexpected frame size")
                result.update(requestedFrames=frames, case=case, repeat=repeat + 1, log=str(log), start=start, end=end)
                results.append(result)
                (artifacts / "results.json").write_text(json.dumps(results, indent=2) + "\n")
                print(json.dumps(result), flush=True)
            finally:
                if pid is not None:
                    try:
                        os.kill(pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                if launcher is not None:
                    launcher.wait(timeout=15)
                if player is not None:
                    player.terminate()
                    player.communicate(timeout=10)
            time.sleep(2)
    print(f"Complete: {artifacts / 'results.json'}", flush=True)


if __name__ == "__main__":
    main()
