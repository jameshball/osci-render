#!/usr/bin/env python3
"""Create a deterministic short RGB/stereo project for end-to-end export QA."""
import argparse
import io
import math
from pathlib import Path
import struct
import wave
import xml.etree.ElementTree as ET


def memory_block_encoding(data):
    """JUCE MemoryBlock's little-endian bit encoding (not RFC base64)."""
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    padded = data + b"\0\0"
    encoded = []
    for bit in range(0, len(data) * 8, 6):
        byte, shift = divmod(bit, 8)
        value = padded[byte] | (padded[byte + 1] << 8)
        encoded.append(alphabet[(value >> shift) & 63])
    return str(len(data)) + "." + "".join(encoded)


def create_fixture(destination):
    duration = 3
    root = ET.Element("motion-project", schema="1")
    composition = ET.SubElement(root, "composition", name="RGB and stereo export check", duration=str(duration), fps="24", bpm="120")
    geometry = b"v -0.6 -0.6 0\nv 0.6 -0.6 0\nv 0.6 0.6 0\nv -0.6 0.6 0\nf 1 2 3 4\n"
    ET.SubElement(composition, "asset", id="1", name="Square.obj", extension=".obj").text = memory_block_encoding(geometry)
    audio = io.BytesIO()
    with wave.open(audio, "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(48000)
        samples = bytearray()
        for frame in range(duration * 48000):
            time = frame / 48000
            # Different channel frequencies expose accidental beam-audio muxing.
            samples.extend(struct.pack("<hh", int(8000 * math.sin(2 * math.pi * 220 * time)), int(6000 * math.sin(2 * math.pi * 440 * time))))
        output.writeframes(samples)
    ET.SubElement(composition, "asset", id="2", name="Stereo test.wav", extension=".wav").text = memory_block_encoding(audio.getvalue())
    track = ET.SubElement(composition, "track", id="3", name="RGB steps", kind="visual")
    for index, color in enumerate(("red", "green", "blue")):
        clip = ET.SubElement(track, "clip", id=str(10 + index), asset="1", name=color.title(), start=str(index), duration="1", offset="0", rate="1")
        for channel in ("red", "green", "blue"):
            ET.SubElement(clip, "property", name=channel, base="1" if channel == color else "0")
    track = ET.SubElement(composition, "track", id="4", name="Stereo reference", kind="audio")
    clip = ET.SubElement(track, "clip", id="13", asset="2", name="220 Hz left / 440 Hz right", start="0", duration=str(duration), offset="0", rate="1")
    ET.SubElement(clip, "property", name="gain", base="1")
    ET.SubElement(clip, "property", name="pan", base="0")
    ET.SubElement(composition, "camera", id="5", name="Output camera")
    for clip in composition.findall("track/clip"):
        clip.set("timeBase", "seconds")
        clip.set("contentBpm", "120")
    xml = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(struct.pack("<II", 0x21324356, len(xml)) + xml + b"\0")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    print(create_fixture(args.destination))
