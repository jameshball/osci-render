#!/usr/bin/env python3
"""Generate dense, high-frequency GPLA workloads from a generated corpus manifest."""
import argparse
import copy
import json
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET

from generate_projects import encode_file, gpla_line


def polygon(count, scale=1.0):
    data = gpla_line((0.5, 0.5, -1.0))
    start = data.index(b'vertexCt')
    vertices = [(scale * math.cos(2 * math.pi * i / count), scale * math.sin(2 * math.pi * i / count), -1.0) for i in range(count + 1)]
    return data[:start] + b'vertexCt' + struct.pack('<q', len(vertices)) + b'VERTICES' + b''.join(struct.pack('<3d', *p) for p in vertices) + data[-48:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = json.loads(args.manifest.read_text())
    template_case = next(c for c in source['cases'] if c['source']['name'] == 'single-line.gpla')
    template = ET.parse(args.manifest.parent / template_case['project']).getroot()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = []
    for count, scale in [(4, 1.0), (256, 1.0), (4096, 1.0), (256, 1e-8)]:
        for frequency in (220, 4200, 20000):
            root = copy.deepcopy(template)
            name = f'polygon-{count}-scale{scale:g}-hz{frequency}'
            file = root.find('files/file')
            file.set('name', name + '.gpla')
            file.text = encode_file(polygon(count, scale))
            parameter = root.find("effects/effect[@id='frequency']/parameter[@id='frequency']")
            parameter.set('max', '20000')
            parameter.set('value', str(frequency))
            parameter.set('smoothValueChange', '0')
            project = name + '.osci'
            (args.output / project).write_bytes(ET.tostring(root, encoding='utf-8', xml_declaration=True))
            case = copy.deepcopy(template_case)
            case.update(id=name, project=project, frequency_hz=frequency, shape_segments=count, shape_scale=scale,
                        source={'kind': 'gpla', 'name': name + '.gpla'})
            cases.append(case)
    # Append MIDI stress without changing the original twelve parameter-driven cases.
    for count in (256, 4096):
        base = next(case for case in cases if case['shape_segments'] == count
                    and case['shape_scale'] == 1.0 and case['frequency_hz'] == 20000)
        for voices in (1, 16):
            case = copy.deepcopy(base)
            root = ET.parse(args.output / base['project']).getroot()
            notes = [127] if voices == 1 else list(range(112, 128))
            name = base['id'] + f'-midi-{voices}voices'
            root.find("intParameters/parameter[@id='voices']").set('value', str(voices))
            root.find("booleanParameters/parameter[@id='midiEnabled']").set('value', '1')
            root.find('files/file').set('name', name + '.gpla')
            project = name + '.osci'
            (args.output / project).write_bytes(ET.tostring(root, encoding='utf-8', xml_declaration=True))
            case.update(id=name, project=project, voices=voices,
                        source={'kind': 'gpla', 'name': name + '.gpla'},
                        pitch_source='midi',
                        pitch_description='Sustained MIDI note 127' if voices == 1 else 'Sustained MIDI notes 112–127',
                        midi={'mode': 'sustain', 'enabled': True, 'requested_notes': voices, 'notes': notes, 'velocity': 80})
            for option, value in (('--voices', str(voices)), ('--midi', 'sustain')):
                case['benchmark_args'][case['benchmark_args'].index(option) + 1] = value
            cases.append(case)
    (args.output / 'manifest.json').write_text(json.dumps({'schemaVersion': 1, 'cases': cases}, indent=2))
    print(f'Generated {len(cases)} shape workloads')


if __name__ == '__main__':
    main()
