"""Generate Pillow GIF fixtures and independently inspect their LZW code streams.
Run from any directory; writes only the adjacent fixture GIFs and JSON manifest.
Requires Pillow. No osci decoder code is imported or executed.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image, __version__ as pillow_version

ROOT = Path(__file__).resolve().parent


def inspect(data):
    """Decode the first GIF image with a string dictionary, recording branch coverage."""
    pos = 13
    if data[10] & 128:
        pos += 3 * (2 << (data[10] & 7))
    while data[pos] == 0x21:
        pos += 2
        while data[pos]:
            pos += data[pos] + 1
        pos += 1
    assert data[pos] == 0x2C
    flags = data[pos + 9]
    pos += 10
    if flags & 128:
        pos += 3 * (2 << (flags & 7))
    minimum = data[pos]
    pos += 1
    packed = bytearray()
    while data[pos]:
        size = data[pos]
        packed += data[pos + 1:pos + 1 + size]
        pos += size + 1
    bit = 0
    clear, end = 1 << minimum, (1 << minimum) + 1
    width, table, previous = minimum + 1, [], None
    stats = dict(max_code_width=0, clear_codes=0, saturated_dictionaries=0,
                 clears_after_saturation=0, kwkwk_codes=0, end_codes=0)
    output = bytearray()
    saturated = False
    while bit + width <= len(packed) * 8:
        code = sum(((packed[(bit + i) // 8] >> ((bit + i) % 8)) & 1) << i for i in range(width))
        bit += width
        stats['max_code_width'] = max(stats['max_code_width'], width)
        if code == clear:
            stats['clear_codes'] += 1
            stats['clears_after_saturation'] += int(saturated)
            saturated = False
            table = [bytes([i]) for i in range(clear)] + [None, None]
            previous, width = None, minimum + 1
            continue
        if code == end:
            stats['end_codes'] += 1
            break
        if code == len(table) and previous is not None:
            entry = previous + previous[:1]
            stats['kwkwk_codes'] += 1
        else:
            assert 0 <= code < len(table) and table[code] is not None
            entry = table[code]
        output += entry
        if previous is not None and len(table) < 4096:
            table.append(previous + entry[:1])
            if len(table) == 4096:
                saturated = True
                stats['saturated_dictionaries'] += 1
            if len(table) == (1 << width) and width < 12:
                width += 1
        previous = entry
    assert stats['end_codes'] == 1
    return bytes(output), stats


def write(name, width, height, indices):
    image = Image.new('P', (width, height))
    image.putpalette([component for i in range(256) for component in (i, (i * 7) % 256, (i * 13) % 256)])
    image.putdata(indices)
    path = ROOT / name
    image.save(path, format='GIF', optimize=False, interlace=False)
    data = path.read_bytes()
    decoded, stats = inspect(data)
    assert decoded == bytes(indices)
    return dict(file=name, width=width, height=height, encoded_bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest(), **stats)


state = 17
random_indices = []
for _ in range(128 * 128):
    state = (state * 1664525 + 1013904223) & 0xffffffff
    random_indices.append(state >> 24)
random = write('lzw_saturation.gif', 128, 128, random_indices)
assert random['max_code_width'] == 12
assert random['saturated_dictionaries'] >= 2 and random['clears_after_saturation'] >= 2
repetitive = write('lzw_kwkwk.gif', 128, 16, [1] * (128 * 16))
assert repetitive['kwkwk_codes'] >= 1
manifest = dict(generator='Pillow', pillow_version=pillow_version, fixtures=[random, repetitive])
(ROOT / 'raster_lzw_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
