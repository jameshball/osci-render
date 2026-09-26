#!/usr/bin/env python3
"""Report lit runs and unused drawing time in one exported XYRGB beam cycle.

This measures allocation, not geometric correctness: a long lit run can still
miss a corner or stroke endpoint. Requires numpy; reads only the selected cycle.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("signal", type=Path)
parser.add_argument("--at", type=float, default=0, help="Time in seconds; aligns to the containing beam cycle")
parser.add_argument("--beam-rate", type=float, default=60)
parser.add_argument("--output", type=Path)
args = parser.parse_args()
if not math.isfinite(args.at) or args.at < 0 or not math.isfinite(args.beam_rate) or args.beam_rate <= 0:
    parser.error("Use a finite, nonnegative time and positive beam rate")

with args.signal.open("rb") as stream:
    if stream.read(4) != b"RIFF":
        parser.error("Expected a RIFF WAV export")
    stream.read(4)
    if stream.read(4) != b"WAVE":
        parser.error("Expected WAVE data")
    format_data = None
    data_offset = data_size = None
    while header := stream.read(8):
        if len(header) != 8:
            parser.error("Truncated WAV chunk header")
        tag, size = struct.unpack("<4sI", header)
        if tag == b"fmt ":
            if size > 65536:
                parser.error("Invalid WAV format chunk")
            format_data = stream.read(size)
        elif tag == b"data":
            data_offset, data_size = stream.tell(), size
            stream.seek(size, 1)
        else:
            stream.seek(size, 1)
        if size % 2:
            stream.seek(1, 1)
        if format_data is not None and data_offset is not None:
            break
    if format_data is None or len(format_data) < 16 or data_offset is None:
        parser.error("Missing WAV format or signal data")
    encoding, channels, rate, _, alignment, bits = struct.unpack_from("<HHIIHH", format_data)
    if encoding != 3 or channels != 5 or bits != 32 or alignment != 20 or rate == 0:
        parser.error("Expected a five-channel float32 XYRGB WAV")
    if args.beam_rate > rate:
        parser.error("Beam rate must not exceed the signal sample rate")
    if args.at >= (data_size // alignment) / rate:
        parser.error("Requested time is outside the export")
    cycle_position = args.at * args.beam_rate
    if not math.isfinite(cycle_position):
        parser.error("Requested cycle overflows the clock")
    cycle = math.floor(cycle_position)
    first_frame = cycle * rate / args.beam_rate
    last_frame = (cycle + 1) * rate / args.beam_rate
    if not math.isfinite(first_frame) or not math.isfinite(last_frame):
        parser.error("Requested cycle overflows the sample clock")
    begin = math.ceil(first_frame)
    end = math.ceil(last_frame)
    if end <= begin or end > data_size // alignment:
        parser.error("The requested complete beam cycle is outside the export")
    stream.seek(data_offset + begin * alignment)
    raw = stream.read((end - begin) * alignment)
    if len(raw) != (end - begin) * alignment:
        parser.error("Truncated WAV signal data")
    samples = np.frombuffer(raw, dtype="<f4").reshape(-1, 5)
if not np.isfinite(samples).all():
    parser.error("Selected cycle contains nonfinite signal values")

lit = np.any(samples[:, 2:] != 0, axis=1)
changes = np.flatnonzero(lit[1:] != lit[:-1]) + 1
boundaries = [0, *changes.tolist(), len(samples)]
runs = []
for first, last in zip(boundaries, boundaries[1:]):
    if not lit[first]:
        continue
    runs.append({"firstSample": first, "samples": last - first,
                 "firstXY": samples[first, :2].tolist(), "lastXY": samples[last - 1, :2].tolist(),
                 "firstRGB": samples[first, 2:].tolist()})
report = {"sampleRate": rate, "beamRate": args.beam_rate, "cycle": cycle,
          "firstFrame": begin, "samples": len(samples), "litSamples": int(lit.sum()),
          "darkSamples": int((~lit).sum()), "litRuns": runs}
text = json.dumps(report, indent=2)
if args.output:
    args.output.write_text(text + "\n", encoding="utf-8")
print(text)
