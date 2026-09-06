#!/usr/bin/env python3
"""Generate active curved-LFO workloads from an existing deterministic WAV case."""
import argparse
import copy
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET

from generate_projects import read_template, set_parameter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    seed = next(case for case in manifest['cases'] if case['source']['kind'] == 'audio'
                and case['source']['name'].endswith('.wav') and case['effects'] == 0)
    template = read_template(args.manifest.parent / seed['project'])
    result = copy.deepcopy(manifest)
    result['cases'] = []
    args.output.mkdir(parents=True, exist_ok=True)
    for count in (3, 8, 128):
        for smooth in (False, True):
            project = copy.deepcopy(template)
            for path in ('lfoAssignments', 'envelopes/envAssignments', 'randoms/randomAssignments', 'sidechain/sidechainAssignments'):
                node = project.find(path)
                if node is not None:
                    node.clear()
            assignments = project.find('lfoAssignments')
            if assignments is None:
                assignments = ET.SubElement(project, 'lfoAssignments')
            scale = project.find("effects/effect[@id='scaleX']")
            for flag in ('enabled', 'selected'):
                node = scale.find(flag)
                node.set('value', '1')
                set_parameter(project, 'booleanParameters', node.get('id'), 1)
            for lfo in project.findall('lfos/lfo'):
                index = int(lfo.get('index'))
                lfo.set('smooth', str(int(smooth)))
                for node in list(lfo):
                    lfo.remove(node)
                for i in range(count):
                    ET.SubElement(lfo, 'node', {'time': str(i / (count - 1)),
                                  'value': str(0.5 + 0.45 * math.sin(i * 2.1)),
                                  'curve': str((i % 3 - 1) * 3.0)})
                set_parameter(project, 'floatParameters', f'lfo{index + 1}Rate', index + 1)
                ET.SubElement(assignments, 'assignment', {'lfo': str(index), 'param': 'scaleX',
                              'depth': '0.025', 'bipolar': '1'})
            case = copy.deepcopy(seed)
            case.update(id=f'audio-curve{count}-smooth{int(smooth)}', effects=1, modulation=8,
                        modulation_type='lfo', requires_premium=True, curve_nodes=count, curve_smooth=smooth)
            case['project'] = case['id'] + '.osci'
            assert len(project.findall('lfoAssignments/assignment')) == 8
            assert all(len(lfo.findall('node')) == count for lfo in project.findall('lfos/lfo'))
            (args.output / case['project']).write_bytes(ET.tostring(project, encoding='utf-8', xml_declaration=True))
            result['cases'].append(case)
    result['curve_stress'] = 'All eight LFOs assigned to scaleX; deterministic WAV source; 1–8Hz rates.'
    (args.output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'Generated {len(result["cases"])} active-curve cases in {args.output}')


if __name__ == '__main__':
    main()
