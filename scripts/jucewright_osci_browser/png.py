from __future__ import annotations

import struct
import zlib
from pathlib import Path

from jucewright_browser.errors import StepError


def check_png_not_blank(file: Path, *, crop_bottom_fraction: float = 0.0) -> None:
    data = file.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise StepError("not a png")

    pos = 8
    width = height = bit_depth = color_type = interlace = None
    payload = b""

    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            payload += chunk
        elif kind == b"IEND":
            break

    channels_by_type = {0: 1, 2: 3, 4: 2, 6: 4}
    channels = channels_by_type.get(color_type)
    if bit_depth != 8 or interlace != 0 or channels is None:
        return

    raw = zlib.decompress(payload)
    stride = width * channels
    checked_height = height
    if crop_bottom_fraction > 0.0:
        checked_height = max(1, min(height, int(height * (1.0 - crop_bottom_fraction))))

    prior = bytearray(stride)
    values: list[int] = []
    offset = 0

    for y in range(height):
        filter_type = raw[offset]
        offset += 1
        row = bytearray(raw[offset:offset + stride])
        offset += stride
        recon = bytearray(stride)

        for x, value in enumerate(row):
            a = recon[x - channels] if x >= channels else 0
            b = prior[x]
            c = prior[x - channels] if x >= channels else 0
            if filter_type == 0:
                recon[x] = value
            elif filter_type == 1:
                recon[x] = (value + a) & 255
            elif filter_type == 2:
                recon[x] = (value + b) & 255
            elif filter_type == 3:
                recon[x] = (value + ((a + b) // 2)) & 255
            elif filter_type == 4:
                p = a + b - c
                pa = abs(p - a)
                pb = abs(p - b)
                pc = abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                recon[x] = (value + pr) & 255
            else:
                raise StepError("unsupported png filter")

        prior = recon
        if y >= checked_height:
            continue

        if color_type in (2, 6):
            values.extend(recon[i] for i in range(0, len(recon), channels))
            values.extend(recon[i] for i in range(1, len(recon), channels))
            values.extend(recon[i] for i in range(2, len(recon), channels))
        else:
            values.extend(recon[0::channels])

    if not values:
        raise StepError("empty png")

    minimum = min(values)
    maximum = max(values)
    mean = sum(values) / len(values)
    print(f"min={minimum} max={maximum} mean={mean:.2f} checkedHeight={checked_height}")
    if maximum < 10 or maximum - minimum < 4:
        raise StepError("image appears blank or nearly flat")
