#!/usr/bin/env python3
"""Build and run a JUCE-free CoreAudio tap probe; system capture permission is required."""
import argparse
import json
import math
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import tempfile
import time
import wave


def make_tone(path, seconds):
    rate = 48000
    # Quiet quadrature stereo: the two channels are never simultaneously zero.
    block = b"".join(struct.pack("<hh", int(327 * math.sin(2 * math.pi * 220 * i / rate)),
                                int(327 * math.cos(2 * math.pi * 220 * i / rate))) for i in range(rate))
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(rate)
        for _ in range(math.ceil(seconds)):
            output.writeframesraw(block)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--drift", type=int, choices=[0, 1], default=1)
    parser.add_argument("--auto-start", type=int, choices=[0, 1], default=0)
    parser.add_argument("--physical-frames", type=int, choices=[32, 64], help="Override physical output buffer size independently of capture")
    parser.add_argument("--bundle", type=Path, help="Reuse an already-built probe and its capture permission")
    parser.add_argument("--cases", nargs="+", default=["output-64", "output-32", "tap-only-64", "tap-only-32"],
                        choices=["output-64", "output-32", "tap-only-64", "tap-only-32"])
    args = parser.parse_args()
    if args.seconds < 10 or args.repeats < 1 or args.warmup < 0:
        parser.error("Invalid duration/repeat count")
    root = Path(tempfile.mkdtemp(prefix="native-process-tap-"))
    bundle = args.bundle or root / "Process Tap Probe.app"
    executable = bundle / "Contents/MacOS/ProcessTapProbe"
    if args.bundle is None:
        executable.parent.mkdir(parents=True)
        with (bundle / "Contents/Info.plist").open("wb") as file:
            plistlib.dump({"CFBundleIdentifier": "com.jameshball.ProcessTapProbe", "CFBundleExecutable": "ProcessTapProbe",
                          "CFBundleName": "Process Tap Probe", "CFBundlePackageType": "APPL", "CFBundleVersion": "1",
                          "LSUIElement": True, "NSAudioCaptureUsageDescription": "Measure system audio capture dropouts without recording audio."}, file)
        subprocess.run(["xcrun", "clang++", "-std=c++17", "-O3", "-fobjc-arc", "-fblocks", "-mmacosx-version-min=14.2",
                        str(Path(__file__).with_name("native_process_tap_probe.mm")), "-framework", "Cocoa", "-framework", "CoreAudio",
                        "-o", str(executable)], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(bundle)], check=True)
    tone = root / "tone.wav"
    make_tone(tone, args.seconds + args.warmup + 90)
    print(f"Artifacts: {root}", flush=True)
    results = []
    for repeat in range(args.repeats):
        cases = list(args.cases)
        if repeat % 2:
            cases.reverse()
        for case in cases:
            attached = case.startswith("output-")
            frames = int(case.rsplit("-", 1)[1])
            label = f"{repeat + 1}-{case}"
            config = {"attachedOutput": attached, "frames": frames, "physicalFrames": frames if attached else 64,
                      "drift": bool(args.drift), "autoStart": bool(args.auto_start), "seconds": args.seconds, "warmup": args.warmup}
            if args.physical_frames is not None:
                config["physicalFrames"] = args.physical_frames
            request = root / f"{label}-request.json"
            result_file = root / f"{label}-result.json"
            request.write_text(json.dumps(config))
            print(f"START {label}", flush=True)
            player = subprocess.Popen(["afplay", str(tone)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            try:
                subprocess.run(["open", "-n", "-W", str(bundle), "--stdout", str(root / f"{label}.log"),
                                "--stderr", str(root / f"{label}.log"), "--args", str(request), str(result_file)],
                               check=True, timeout=args.seconds + args.warmup + 60)
                result = json.loads(result_file.read_text())
                result.update(case=case, repeat=repeat + 1)
                print(json.dumps(result), flush=True)
                results.append(result)
                (root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
                if not result.get("validSignal"):
                    raise RuntimeError(f"No valid signal; inspect {result_file} before interpreting this as a dropout")
                if player.poll() is not None:
                    raise RuntimeError("Test source stopped early")
            finally:
                processes = subprocess.run(["pgrep", "-f", "^" + re.escape(str(executable.resolve())) + r"( |$)"],
                                           capture_output=True, text=True, check=False)
                for pid in processes.stdout.split():
                    subprocess.run(["kill", "-TERM", str(pid)], check=False)
                player.terminate()
                player.communicate(timeout=10)
            time.sleep(2)
    print(f"Complete: {root / 'results.json'}", flush=True)


if __name__ == "__main__":
    main()
